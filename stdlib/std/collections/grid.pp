// A fixed-size 2D grid of int, stored row-major in a flat List.
//
// Row-major rather than a list of rows, because a list of lists would need a
// recursive type. The index arithmetic is the whole abstraction.

struct Grid { cells: List<int>, width: int, height: int }

fn grid_new(width: int, height: int, initial: int) -> Grid {
    if width <= 0 or height <= 0 { panic("grid dimensions must be positive"); }
    let cells = list<int>();
    for i in 0..width * height { list_push(cells, initial); }
    return Grid(cells, width, height);
}

fn grid_index(board: Grid, x: int, y: int) -> int {
    if x < 0 or x >= board.width or y < 0 or y >= board.height {
        panic("grid coordinates are out of range");
    }
    return y * board.width + x;
}

fn grid_get(board: Grid, x: int, y: int) -> int {
    return list_at(board.cells, grid_index(board, x, y));
}

fn grid_set(board: Grid, x: int, y: int, value: int) {
    list_put(board.cells, grid_index(board, x, y), value);
}

fn grid_in_bounds(board: Grid, x: int, y: int) -> bool {
    return x >= 0 and x < board.width and y >= 0 and y < board.height;
}

/// Reads a neighbour, returning `fallback` when the coordinates fall outside
/// the grid. Saves every caller writing the same bounds check.
fn grid_get_or(board: Grid, x: int, y: int, fallback: int) -> int {
    if !grid_in_bounds(board, x, y) { return fallback; }
    return grid_get(board, x, y);
}

fn grid_fill(board: Grid, value: int) {
    for i in 0..list_size(board.cells) { list_put(board.cells, i, value); }
}

fn grid_count(board: Grid, value: int) -> int {
    let mut total = 0;
    for i in 0..list_size(board.cells) {
        if list_at(board.cells, i) == value { total = total + 1; }
    }
    return total;
}

/// Sum of the eight surrounding cells, treating out-of-bounds as zero. The
/// shape most cellular automata need.
fn grid_neighbour_sum(board: Grid, x: int, y: int) -> int {
    let mut total = 0;
    for dy in 0..3 {
        for dx in 0..3 {
            if dx == 1 and dy == 1 { continue; }
            total = total + grid_get_or(board, x + dx - 1, y + dy - 1, 0);
        }
    }
    return total;
}

fn grid_to_text(board: Grid) -> str {
    let rows = list<str>();
    for y in 0..board.height {
        let mut line = "";
        for x in 0..board.width { line = line + text(grid_get(board, x, y)); }
        list_push(rows, line);
    }
    return join(rows, "\n");
}
