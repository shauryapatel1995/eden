#include <stdint.h>
#include <stddef.h>

typedef struct {
    int feature;       /* -1 if leaf */
    float threshold;
    int left;
    int right;
    int missing_left;
    float leaf_value;
} native_tree_node_t;

static const native_tree_node_t native_nodes_pointer_555555555385[21] = {
    {3, -196.0f, 1, 10, 1, 0.0},
    {2, 1371.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {1, 86.0f, 4, 7, 1, 0.0},
    {2, 2744.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {1, 257.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -16.0f, 11, 16, 1, 0.0},
    {2, 695.5f, 12, 13, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {1, 429.5f, 14, 15, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2744.0f, 17, 20, 1, 0.0},
    {1, 86.0f, 18, 19, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555554ba[3] = {
    {1, 0.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t* get_native_nodes(uint64_t pc, int* num_nodes) {
    switch (pc) {
        case 0x555555555385:
            *num_nodes = 21;
            return native_nodes_pointer_555555555385;
        case 0x5555555554ba:
            *num_nodes = 3;
            return native_nodes_pointer_5555555554ba;
        default:
            *num_nodes = 0;
            return NULL;
    }
}
