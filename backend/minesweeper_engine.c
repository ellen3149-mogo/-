#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static int idx(int r, int c, int cols) {
    return r * cols + c;
}

static int in_bounds(int r, int c, int rows, int cols) {
    return r >= 0 && r < rows && c >= 0 && c < cols;
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
    int qh = 0, qt = 0;
    if (!queue) return;

    queue[qt++] = idx(start_r, start_c, cols);
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
                Cell *neighbor = &cells[idx(nr, nc, cols)];
                if (!neighbor->is_revealed && !neighbor->has_mine && !neighbor->is_flagged) {
                    queue[qt++] = idx(nr, nc, cols);
                }
            }
        }
    }
    free(queue);
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

static void print_success_json(const GameHeader *header, const Cell *cells) {
    printf("{\"ok\":true,\"rows\":%d,\"cols\":%d,\"mines\":%d,\"revealed\":%d,\"game_over\":%d,\"won\":%d",
           header->rows, header->cols, header->mines, header->revealed_count, header->game_over, header->won);
    print_board_json(header, cells);
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

        print_success_json(&header, cells);
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
        print_success_json(&header, cells);
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
        print_success_json(&header, cells);
        free(cells);
        return 0;
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

    print_success_json(&header, cells);
    free(cells);
    return 0;
}
