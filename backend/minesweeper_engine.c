#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#ifdef _WIN32
#include <io.h>
#endif

typedef struct {
    unsigned char has_mine;
    unsigned char is_revealed;
    unsigned char is_flagged;
    unsigned char adjacent_mines;
} Cell;

typedef struct {
    int rows;
    int cols;
    int mines;
    int revealed_count;
    int game_over;
    int won;
    unsigned int seed;
} GameHeader;

typedef struct {
    int head;
    int tail;
    int count;
    int next_scan;
} HistoryHeader;

typedef struct {
    int used;
    int id;
    int action;
    int row;
    int col;
    int revealed;
    int game_over;
    int won;
    int timestamp;
    int next;
} HistoryNode;

#define MAX_HISTORY 4096

static int idx(int r, int c, int cols) {
    return r * cols + c;
}

static int in_bounds(int r, int c, int rows, int cols) {
    return r >= 0 && r < rows && c >= 0 && c < cols;
}

static int build_sidecar_path(char *buffer, size_t len, const char *state_file, const char *suffix) {
    if (!buffer || !state_file || !suffix) return 0;
    int written = snprintf(buffer, len, "%s%s", state_file, suffix);
    return written > 0 && (size_t)written < len;
}

static int read_entire_file(const char *path, unsigned char **out_data, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    long size;
    unsigned char *data;
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (!data && size > 0) {
        fclose(fp);
        return 0;
    }
    if (size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    *out_data = data;
    *out_size = (size_t)size;
    return 1;
}

static int write_entire_file(const char *path, const unsigned char *data, size_t size) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    if (size > 0 && fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int init_undo_file(const char *state_file) {
    char undo_path[1024];
    if (!build_sidecar_path(undo_path, sizeof(undo_path), state_file, ".undo")) return 0;
    return write_entire_file(undo_path, NULL, 0);
}

static int push_undo_snapshot(const char *state_file) {
    char undo_path[1024];
    FILE *fp;
    unsigned char *snapshot = NULL;
    size_t snapshot_size = 0;
    uint32_t sz;
    if (!build_sidecar_path(undo_path, sizeof(undo_path), state_file, ".undo")) return 0;
    if (!read_entire_file(state_file, &snapshot, &snapshot_size)) return 0;
    fp = fopen(undo_path, "ab");
    if (!fp) {
        free(snapshot);
        return 0;
    }
    sz = (uint32_t)snapshot_size;
    if (fwrite(&sz, sizeof(uint32_t), 1, fp) != 1) {
        free(snapshot);
        fclose(fp);
        return 0;
    }
    if (snapshot_size > 0 && fwrite(snapshot, 1, snapshot_size, fp) != snapshot_size) {
        free(snapshot);
        fclose(fp);
        return 0;
    }
    free(snapshot);
    fclose(fp);
    return 1;
}

static int pop_undo_snapshot(const char *state_file) {
    char undo_path[1024];
    FILE *fp;
    long file_size;
    long pos = 0;
    long last_entry_offset = -1;
    uint32_t sz = 0;
    unsigned char *snapshot = NULL;
    if (!build_sidecar_path(undo_path, sizeof(undo_path), state_file, ".undo")) return 0;
    fp = fopen(undo_path, "rb+");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    while (pos < file_size) {
        if (fread(&sz, sizeof(uint32_t), 1, fp) != 1) {
            fclose(fp);
            return 0;
        }
        if ((long)sz < 0 || pos + (long)sizeof(uint32_t) + (long)sz > file_size) {
            fclose(fp);
            return 0;
        }
        last_entry_offset = pos;
        if (fseek(fp, (long)sz, SEEK_CUR) != 0) {
            fclose(fp);
            return 0;
        }
        pos = ftell(fp);
    }
    if (last_entry_offset < 0) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, last_entry_offset, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    if (fread(&sz, sizeof(uint32_t), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    snapshot = (unsigned char *)malloc((size_t)sz);
    if (!snapshot && sz > 0) {
        fclose(fp);
        return 0;
    }
    if (sz > 0 && fread(snapshot, 1, (size_t)sz, fp) != (size_t)sz) {
        free(snapshot);
        fclose(fp);
        return 0;
    }
#ifdef _WIN32
    if (_chsize(_fileno(fp), (long)last_entry_offset) != 0) {
        free(snapshot);
        fclose(fp);
        return 0;
    }
#endif
    fclose(fp);
    if (!write_entire_file(state_file, snapshot, (size_t)sz)) {
        free(snapshot);
        return 0;
    }
    free(snapshot);
    return 1;
}

static int init_history_file(const char *state_file) {
    char history_path[1024];
    FILE *fp;
    HistoryHeader header;
    HistoryNode zero = {0};
    if (!build_sidecar_path(history_path, sizeof(history_path), state_file, ".hist")) return 0;
    fp = fopen(history_path, "wb");
    if (!fp) return 0;
    header.head = -1;
    header.tail = -1;
    header.count = 0;
    header.next_scan = 0;
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    for (int i = 0; i < MAX_HISTORY; i++) {
        if (fwrite(&zero, sizeof(zero), 1, fp) != 1) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

static int load_history(const char *state_file, HistoryHeader *header, HistoryNode *nodes) {
    char history_path[1024];
    FILE *fp;
    if (!build_sidecar_path(history_path, sizeof(history_path), state_file, ".hist")) return 0;
    fp = fopen(history_path, "rb");
    if (!fp) return 0;
    if (fread(header, sizeof(*header), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (fread(nodes, sizeof(HistoryNode), MAX_HISTORY, fp) != MAX_HISTORY) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int save_history(const char *state_file, const HistoryHeader *header, const HistoryNode *nodes) {
    char history_path[1024];
    FILE *fp;
    if (!build_sidecar_path(history_path, sizeof(history_path), state_file, ".hist")) return 0;
    fp = fopen(history_path, "wb");
    if (!fp) return 0;
    if (fwrite(header, sizeof(*header), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (fwrite(nodes, sizeof(HistoryNode), MAX_HISTORY, fp) != MAX_HISTORY) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int append_history(const char *state_file, int action, int row, int col, const GameHeader *header) {
    HistoryHeader h;
    HistoryNode nodes[MAX_HISTORY];
    int slot = -1;
    if (!load_history(state_file, &h, nodes)) return 0;
    for (int i = 0; i < MAX_HISTORY; i++) {
        int candidate = (h.next_scan + i) % MAX_HISTORY;
        if (!nodes[candidate].used) {
            slot = candidate;
            h.next_scan = (candidate + 1) % MAX_HISTORY;
            break;
        }
    }
    if (slot < 0) return 0;
    nodes[slot].used = 1;
    nodes[slot].id = slot;
    nodes[slot].action = action;
    nodes[slot].row = row;
    nodes[slot].col = col;
    nodes[slot].revealed = header->revealed_count;
    nodes[slot].game_over = header->game_over;
    nodes[slot].won = header->won;
    nodes[slot].timestamp = (int)time(NULL);
    nodes[slot].next = -1;
    if (h.tail >= 0 && h.tail < MAX_HISTORY && nodes[h.tail].used) {
        nodes[h.tail].next = slot;
    }
    if (h.head < 0) h.head = slot;
    h.tail = slot;
    h.count++;
    return save_history(state_file, &h, nodes);
}

static int save_game(const char *path, const GameHeader *header, const Cell *cells) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    if (fwrite(header, sizeof(GameHeader), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (fwrite(cells, sizeof(Cell), (size_t)(header->rows * header->cols), fp) != (size_t)(header->rows * header->cols)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int load_game(const char *path, GameHeader *header, Cell **cells_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    if (fread(header, sizeof(GameHeader), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (header->rows <= 0 || header->cols <= 0 || header->rows > 40 || header->cols > 40) {
        fclose(fp);
        return 0;
    }
    Cell *cells = (Cell *)calloc((size_t)(header->rows * header->cols), sizeof(Cell));
    if (!cells) {
        fclose(fp);
        return 0;
    }
    if (fread(cells, sizeof(Cell), (size_t)(header->rows * header->cols), fp) != (size_t)(header->rows * header->cols)) {
        free(cells);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    *cells_out = cells;
    return 1;
}

static int count_adjacent_mines(const Cell *cells, int rows, int cols, int r, int c) {
    int count = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr;
            int nc = c + dc;
            if (in_bounds(nr, nc, rows, cols) && cells[idx(nr, nc, cols)].has_mine) {
                count++;
            }
        }
    }
    return count;
}

static void calculate_adjacency(Cell *cells, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (cells[idx(r, c, cols)].has_mine) continue;
            cells[idx(r, c, cols)].adjacent_mines = (unsigned char)count_adjacent_mines(cells, rows, cols, r, c);
        }
    }
}

static void reveal_all_mines(Cell *cells, int rows, int cols) {
    for (int i = 0; i < rows * cols; i++) {
        if (cells[i].has_mine) {
            cells[i].is_revealed = 1;
        }
    }
}

static void flood_reveal(Cell *cells, int rows, int cols, int start_r, int start_c, int *revealed_count) {
    int max = rows * cols;
    int *queue = (int *)malloc((size_t)max * sizeof(int));
    unsigned char *queued = (unsigned char *)calloc((size_t)max, sizeof(unsigned char));
    int qh = 0, qt = 0;
    if (!queue || !queued) {
        free(queue);
        free(queued);
        return;
    }

    int start = idx(start_r, start_c, cols);
    queue[qt++] = start;
    queued[start] = 1;
    while (qh < qt) {
        int current = queue[qh++];
        int r = current / cols;
        int c = current % cols;
        Cell *cell = &cells[current];

        if (cell->is_revealed || cell->is_flagged) continue;
        cell->is_revealed = 1;
        (*revealed_count)++;

        if (cell->adjacent_mines != 0) continue;
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                if (dr == 0 && dc == 0) continue;
                int nr = r + dr;
                int nc = c + dc;
                if (!in_bounds(nr, nc, rows, cols)) continue;
                int nindex = idx(nr, nc, cols);
                Cell *neighbor = &cells[nindex];
                if (!neighbor->is_revealed && !neighbor->has_mine && !neighbor->is_flagged && !queued[nindex]) {
                    queue[qt++] = nindex;
                    queued[nindex] = 1;
                }
            }
        }
    }
    free(queue);
    free(queued);
}

static void print_board_json(const GameHeader *header, const Cell *cells) {
    printf(",\"board\":[");
    for (int r = 0; r < header->rows; r++) {
        printf("[");
        for (int c = 0; c < header->cols; c++) {
            const Cell *cell = &cells[idx(r, c, header->cols)];
            char out = '#';
            if (cell->is_revealed) {
                if (cell->has_mine) out = '*';
                else if (cell->adjacent_mines > 0) out = (char)('0' + cell->adjacent_mines);
                else out = '.';
            } else if (cell->is_flagged) {
                out = 'F';
            }
            printf("\"%c\"", out);
            if (c < header->cols - 1) printf(",");
        }
        printf("]");
        if (r < header->rows - 1) printf(",");
    }
    printf("]");
}

static void print_history_json(const char *state_file) {
    HistoryHeader h;
    HistoryNode nodes[MAX_HISTORY];
    int cursor;
    int emitted = 0;
    if (!load_history(state_file, &h, nodes)) {
        printf(",\"history\":[]");
        return;
    }
    printf(",\"history\":[");
    cursor = h.head;
    while (cursor >= 0 && cursor < MAX_HISTORY && nodes[cursor].used && emitted < 100) {
        HistoryNode *n = &nodes[cursor];
        printf("{\"id\":\"%d\",\"action\":\"%s\",\"r\":%d,\"c\":%d,\"revealed\":%d,\"game_over\":%d,\"won\":%d,\"timestamp\":%d,\"next\":\"%d\"}",
               n->id,
               n->action == 1 ? "reveal" : (n->action == 2 ? "flag" : "undo"),
               n->row,
               n->col,
               n->revealed,
               n->game_over,
               n->won,
               n->timestamp,
               n->next);
        emitted++;
        cursor = n->next;
        if (cursor >= 0 && emitted < 100) printf(",");
    }
    printf("]");
}

static void print_success_json(const char *state_file, const GameHeader *header, const Cell *cells) {
    printf("{\"ok\":true,\"rows\":%d,\"cols\":%d,\"mines\":%d,\"revealed\":%d,\"game_over\":%d,\"won\":%d",
           header->rows, header->cols, header->mines, header->revealed_count, header->game_over, header->won);
    print_board_json(header, cells);
    print_history_json(state_file);
    printf("}\n");
}

static void print_error_json(const char *message) {
    printf("{\"ok\":false,\"error\":\"%s\"}\n", message);
}

static int place_mines(Cell *cells, int rows, int cols, int mines, unsigned int seed) {
    int total = rows * cols;
    if (mines >= total) return 0;
    srand(seed);
    int placed = 0;
    while (placed < mines) {
        int pos = rand() % total;
        if (!cells[pos].has_mine) {
            cells[pos].has_mine = 1;
            placed++;
        }
    }
    return 1;
}

static int check_win(GameHeader *header) {
    int safe_cells = header->rows * header->cols - header->mines;
    if (header->revealed_count >= safe_cells) {
        header->won = 1;
        header->game_over = 1;
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_error_json("Missing command");
        return 1;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (argc != 6) {
            print_error_json("Usage: init <state_file> <rows> <cols> <mines>");
            return 1;
        }
        const char *state_file = argv[2];
        int rows = atoi(argv[3]);
        int cols = atoi(argv[4]);
        int mines = atoi(argv[5]);
        if (rows < 5 || cols < 5 || rows > 30 || cols > 30) {
            print_error_json("Rows and cols must be between 5 and 30");
            return 1;
        }
        if (mines < 1 || mines >= rows * cols) {
            print_error_json("Invalid mine count");
            return 1;
        }

        GameHeader header = {0};
        header.rows = rows;
        header.cols = cols;
        header.mines = mines;
        header.revealed_count = 0;
        header.game_over = 0;
        header.won = 0;
        header.seed = (unsigned int)time(NULL);

        Cell *cells = (Cell *)calloc((size_t)(rows * cols), sizeof(Cell));
        if (!cells) {
            print_error_json("Memory allocation failed");
            return 1;
        }

        if (!place_mines(cells, rows, cols, mines, header.seed)) {
            free(cells);
            print_error_json("Mine placement failed");
            return 1;
        }
        calculate_adjacency(cells, rows, cols);

        if (!save_game(state_file, &header, cells)) {
            free(cells);
            print_error_json("Failed to save state");
            return 1;
        }

        if (!init_undo_file(state_file) || !init_history_file(state_file)) {
            free(cells);
            print_error_json("Failed to initialize C sidecar data");
            return 1;
        }

        print_success_json(state_file, &header, cells);
        free(cells);
        return 0;
    }

    if (argc < 3) {
        print_error_json("Missing state file");
        return 1;
    }

    const char *state_file = argv[2];
    GameHeader header;
    Cell *cells = NULL;
    if (!load_game(state_file, &header, &cells)) {
        print_error_json("Failed to load state");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        print_success_json(state_file, &header, cells);
        free(cells);
        return 0;
    }

    if (strcmp(argv[1], "undo") == 0) {
        free(cells);
        if (!pop_undo_snapshot(state_file)) {
            print_error_json("No undo step available");
            return 1;
        }
        if (!load_game(state_file, &header, &cells)) {
            print_error_json("Failed to load state after undo");
            return 1;
        }
        append_history(state_file, 3, -1, -1, &header);
        print_success_json(state_file, &header, cells);
        free(cells);
        return 0;
    }

    if (argc != 5) {
        free(cells);
        print_error_json("Usage: <reveal|flag> <state_file> <row> <col>");
        return 1;
    }

    int row = atoi(argv[3]);
    int col = atoi(argv[4]);
    if (!in_bounds(row, col, header.rows, header.cols)) {
        free(cells);
        print_error_json("Position out of range");
        return 1;
    }

    Cell *target = &cells[idx(row, col, header.cols)];

    if (header.game_over) {
        print_success_json(state_file, &header, cells);
        free(cells);
        return 0;
    }

    if (!push_undo_snapshot(state_file)) {
        free(cells);
        print_error_json("Failed to push undo snapshot");
        return 1;
    }

    if (strcmp(argv[1], "flag") == 0) {
        if (!target->is_revealed) {
            target->is_flagged = target->is_flagged ? 0 : 1;
        }
    } else if (strcmp(argv[1], "reveal") == 0) {
        if (!target->is_flagged && !target->is_revealed) {
            if (target->has_mine) {
                target->is_revealed = 1;
                header.game_over = 1;
                header.won = 0;
                reveal_all_mines(cells, header.rows, header.cols);
            } else {
                flood_reveal(cells, header.rows, header.cols, row, col, &header.revealed_count);
                check_win(&header);
            }
        }
    } else {
        free(cells);
        print_error_json("Unknown command");
        return 1;
    }

    if (!save_game(state_file, &header, cells)) {
        free(cells);
        print_error_json("Failed to save state");
        return 1;
    }

    append_history(state_file, strcmp(argv[1], "reveal") == 0 ? 1 : 2, row, col, &header);
    print_success_json(state_file, &header, cells);
    free(cells);
    return 0;
}
