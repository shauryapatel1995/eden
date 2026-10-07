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

static const native_tree_node_t native_nodes_spatial_555555555b82[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 0.0f, 3, 6, 1, 0.0},
    {3, -154.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -71.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555a44[9] = {
    {3, -3.5f, 1, 6, 1, 0.0},
    {3, -4.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 1.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 3.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555c8a[9] = {
    {2, 3.5f, 1, 8, 1, 0.0},
    {2, 1.5f, 2, 5, 1, 0.0},
    {5, 1.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -6.0f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555eb8[11] = {
    {2, 3.5f, 1, 8, 1, 0.0},
    {4, 9.38249922e+13f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 1.17281152e+14f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 1.40737311e+14f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555557f4[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -6917.0f, 3, 6, 1, 0.0},
    {3, -39.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 5.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555a3a[9] = {
    {5, 1.0f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -2.5f, 5, 8, 1, 0.0},
    {4, 9.38249922e+13f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_55555555605b[9] = {
    {2, 2.5f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -1.5f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555c91[7] = {
    {5, 1.0f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -2.5f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555563bd[11] = {
    {2, 3.5f, 1, 6, 1, 0.0},
    {4, 9.38249922e+13f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 0.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 10, 1, 0.0},
    {5, 0.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556575[7] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 6, 1, 0.0},
    {5, 0.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_8159b108f5[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -1.0f, 3, 6, 1, 0.0},
    {3, -18.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -162.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556053[9] = {
    {2, 3.5f, 1, 8, 1, 0.0},
    {3, -1.5f, 2, 5, 1, 0.0},
    {3, -119.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2.5f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556411[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 5, 1, 0.0},
    {5, 0.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 1.5f, 7, 10, 1, 0.0},
    {5, 30545.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c64ff[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555565a4[9] = {
    {2, 1.5f, 1, 8, 1, 0.0},
    {3, -430.0f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, -12655.0f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555573[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -1.0f, 3, 6, 1, 0.0},
    {3, -8.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -492.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_55555555746b[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 3.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 1.5f, 7, 10, 1, 0.0},
    {3, -8.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555556234[9] = {
    {2, 3.5f, 1, 8, 1, 0.0},
    {2, 2.5f, 2, 5, 1, 0.0},
    {3, -113.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_55555555624e[11] = {
    {4, 9.38249922e+13f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 0.0f, 5, 8, 1, 0.0},
    {2, 1.5f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_55555555623c[7] = {
    {4, 9.38249922e+13f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 0.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555556245[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555a3e[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c6468[7] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 1.40737311e+14f, 3, 6, 1, 0.0},
    {5, -54526.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c64af[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c64d7[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555b59[7] = {
    {4, 9.38249922e+13f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -163.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 0.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c6418[7] = {
    {5, 54528.0f, 1, 4, 1, 0.0},
    {5, -54530.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 3.5f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c6440[11] = {
    {2, 3.5f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -109055.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 1.40737311e+14f, 7, 10, 1, 0.0},
    {5, -54526.5f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555556afe[11] = {
    {5, 9.0f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -2.0f, 5, 8, 1, 0.0},
    {2, 3.5f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 2.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556057[5] = {
    {2, 2.5f, 1, 4, 1, 0.0},
    {2, 1.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556256[5] = {
    {2, 3.5f, 1, 4, 1, 0.0},
    {5, 0.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555556d3[11] = {
    {5, 5.0f, 1, 8, 1, 0.0},
    {5, 1.0f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2.5f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 14.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c647c[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c6513[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555567[5] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -512.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555557e9[11] = {
    {3, -396.0f, 1, 4, 1, 0.0},
    {2, 2.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -37.0f, 5, 8, 1, 0.0},
    {2, 1.5f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c62af[7] = {
    {3, -237.0f, 1, 4, 1, 0.0},
    {4, 1.40737311e+14f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 1.40737311e+14f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555565c2[7] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 6, 1, 0.0},
    {5, 0.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_55555555642f[7] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 0.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555b87[7] = {
    {2, 1.5f, 1, 6, 1, 0.0},
    {4, 9.38249922e+13f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555557c4d[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 5, 1, 0.0},
    {3, -339.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -424.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 772.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555555d3[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {3, -240.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -347.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555556db[11] = {
    {5, 578.0f, 1, 6, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 1.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555556e4[11] = {
    {2, 3.5f, 1, 6, 1, 0.0},
    {2, 2.5f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 578.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555556ed[11] = {
    {5, 2318.0f, 1, 6, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 430.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 1.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c62c3[5] = {
    {4, 1.40737311e+14f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555558513[5] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555556f5[11] = {
    {5, 578.0f, 1, 6, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 69.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 1.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555556a63[7] = {
    {3, -4.5f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555558aa4[7] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -15.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555555f5[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {3, -464.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -11693.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 7, 10, 1, 0.0},
    {5, -11693.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555b28[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -511.5f, 7, 10, 1, 0.0},
    {5, 4.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555f50[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 6, 1, 0.0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c6268[5] = {
    {2, 3.5f, 1, 4, 1, 0.0},
    {5, 5632.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_8159b108ef[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555584fe[7] = {
    {3, -1.5f, 1, 4, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555fef[7] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {5, 0.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 26604.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555b65[9] = {
    {4, 9.38249922e+13f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -4.5f, 3, 6, 1, 0.0},
    {2, 2.5f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555555e8[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 15711.0f, 3, 6, 1, 0.0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555558263[11] = {
    {5, -335.0f, 1, 8, 1, 0.0},
    {3, -9.0f, 2, 5, 1, 0.0},
    {3, -11.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 3.5f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555563db[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 2.5f, 2, 5, 1, 0.0},
    {3, -500.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -479.0f, 7, 10, 1, 0.0},
    {5, 2107.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555557ee1[9] = {
    {2, 3.5f, 1, 8, 1, 0.0},
    {2, 2.5f, 2, 5, 1, 0.0},
    {2, 1.5f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -6.0f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555584d5[7] = {
    {4, 9.38249922e+13f, 1, 4, 1, 0.0},
    {2, 2.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555558233[11] = {
    {5, 691.0f, 1, 6, 1, 0.0},
    {3, -8.0f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -8.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556a81[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556a20[11] = {
    {2, 3.5f, 1, 8, 1, 0.0},
    {2, 2.5f, 2, 5, 1, 0.0},
    {2, 1.5f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 1.17281152e+14f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 1.17281152e+14f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555b41[7] = {
    {2, 1.5f, 1, 6, 1, 0.0},
    {4, 9.38249922e+13f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555557c52[7] = {
    {2, 1.5f, 1, 6, 1, 0.0},
    {3, -342.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -58.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555ff3[11] = {
    {5, 0.0f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 2.5f, 7, 10, 1, 0.0},
    {3, -49.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_55555555747c[11] = {
    {5, 1.0f, 1, 8, 1, 0.0},
    {4, 9.38249922e+13f, 2, 5, 1, 0.0},
    {3, -342.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -58.0f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 1.5f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555800[11] = {
    {3, -507.0f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 3.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 206.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555fae[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 3.5f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 1.5f, 7, 10, 1, 0.0},
    {4, 9.38249922e+13f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555558313[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {3, -4.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 691.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -1.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555557ee9[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_55555555749c[11] = {
    {3, -94.0f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -10.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 3.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -10.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556422[7] = {
    {5, 0.0f, 1, 6, 1, 0.0},
    {2, 2.5f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555556e8[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 6, 1, 0.0},
    {5, 1897.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556419[9] = {
    {2, 1.5f, 1, 8, 1, 0.0},
    {5, 210.0f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 3167.0f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555556d6[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 5410.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 2318.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_7ffff56c6aa0[11] = {
    {2, 3.5f, 1, 6, 1, 0.0},
    {4, 9.38249922e+13f, 2, 5, 1, 0.0},
    {5, 1.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555582a4[11] = {
    {3, -1.5f, 1, 6, 1, 0.0},
    {3, -3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 8590.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 10, 1, 0.0},
    {5, -8648.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_5555555556df[9] = {
    {2, 3.5f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 6, 1, 0.0},
    {5, 6250.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_55555555642b[11] = {
    {2, 2.5f, 1, 6, 1, 0.0},
    {2, 1.5f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 0.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 140.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_5555555583a7[11] = {
    {5, 691.0f, 1, 6, 1, 0.0},
    {3, -3.5f, 2, 5, 1, 0.0},
    {4, 9.38249922e+13f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -1.5f, 7, 10, 1, 0.0},
    {5, 8850.33333f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555555b5d[11] = {
    {5, -26009.0f, 1, 6, 1, 0.0},
    {2, 2.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 3.5f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_spatial_555555556006[7] = {
    {5, 0.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 3, 6, 1, 0.0},
    {5, 1.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_spatial_555555555617[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, 3.5f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, -89.1666667f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {4, 9.38249922e+13f, 7, 10, 1, 0.0},
    {5, -1916.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t* get_native_nodes(uint64_t pc, int* num_nodes) {
    switch (pc) {
        case 0x555555555b82:
            *num_nodes = 9;
            return native_nodes_spatial_555555555b82;
        case 0x555555555a44:
            *num_nodes = 9;
            return native_nodes_spatial_555555555a44;
        case 0x555555555c8a:
            *num_nodes = 9;
            return native_nodes_spatial_555555555c8a;
        case 0x555555555eb8:
            *num_nodes = 11;
            return native_nodes_spatial_555555555eb8;
        case 0x5555555557f4:
            *num_nodes = 9;
            return native_nodes_spatial_5555555557f4;
        case 0x555555555a3a:
            *num_nodes = 9;
            return native_nodes_spatial_555555555a3a;
        case 0x55555555605b:
            *num_nodes = 9;
            return native_nodes_spatial_55555555605b;
        case 0x555555555c91:
            *num_nodes = 7;
            return native_nodes_spatial_555555555c91;
        case 0x5555555563bd:
            *num_nodes = 11;
            return native_nodes_spatial_5555555563bd;
        case 0x555555556575:
            *num_nodes = 7;
            return native_nodes_spatial_555555556575;
        case 0x8159b108f5:
            *num_nodes = 9;
            return native_nodes_spatial_8159b108f5;
        case 0x555555556053:
            *num_nodes = 9;
            return native_nodes_spatial_555555556053;
        case 0x555555556411:
            *num_nodes = 11;
            return native_nodes_spatial_555555556411;
        case 0x7ffff56c64ff:
            *num_nodes = 1;
            return native_nodes_spatial_7ffff56c64ff;
        case 0x5555555565a4:
            *num_nodes = 9;
            return native_nodes_spatial_5555555565a4;
        case 0x555555555573:
            *num_nodes = 9;
            return native_nodes_spatial_555555555573;
        case 0x55555555746b:
            *num_nodes = 11;
            return native_nodes_spatial_55555555746b;
        case 0x555555556234:
            *num_nodes = 9;
            return native_nodes_spatial_555555556234;
        case 0x55555555624e:
            *num_nodes = 11;
            return native_nodes_spatial_55555555624e;
        case 0x55555555623c:
            *num_nodes = 7;
            return native_nodes_spatial_55555555623c;
        case 0x555555556245:
            *num_nodes = 1;
            return native_nodes_spatial_555555556245;
        case 0x555555555a3e:
            *num_nodes = 1;
            return native_nodes_spatial_555555555a3e;
        case 0x7ffff56c6468:
            *num_nodes = 7;
            return native_nodes_spatial_7ffff56c6468;
        case 0x7ffff56c64af:
            *num_nodes = 1;
            return native_nodes_spatial_7ffff56c64af;
        case 0x7ffff56c64d7:
            *num_nodes = 1;
            return native_nodes_spatial_7ffff56c64d7;
        case 0x555555555b59:
            *num_nodes = 7;
            return native_nodes_spatial_555555555b59;
        case 0x7ffff56c6418:
            *num_nodes = 7;
            return native_nodes_spatial_7ffff56c6418;
        case 0x7ffff56c6440:
            *num_nodes = 11;
            return native_nodes_spatial_7ffff56c6440;
        case 0x555555556afe:
            *num_nodes = 11;
            return native_nodes_spatial_555555556afe;
        case 0x555555556057:
            *num_nodes = 5;
            return native_nodes_spatial_555555556057;
        case 0x555555556256:
            *num_nodes = 5;
            return native_nodes_spatial_555555556256;
        case 0x5555555556d3:
            *num_nodes = 11;
            return native_nodes_spatial_5555555556d3;
        case 0x7ffff56c647c:
            *num_nodes = 1;
            return native_nodes_spatial_7ffff56c647c;
        case 0x7ffff56c6513:
            *num_nodes = 1;
            return native_nodes_spatial_7ffff56c6513;
        case 0x555555555567:
            *num_nodes = 5;
            return native_nodes_spatial_555555555567;
        case 0x5555555557e9:
            *num_nodes = 11;
            return native_nodes_spatial_5555555557e9;
        case 0x7ffff56c62af:
            *num_nodes = 7;
            return native_nodes_spatial_7ffff56c62af;
        case 0x5555555565c2:
            *num_nodes = 7;
            return native_nodes_spatial_5555555565c2;
        case 0x55555555642f:
            *num_nodes = 7;
            return native_nodes_spatial_55555555642f;
        case 0x555555555b87:
            *num_nodes = 7;
            return native_nodes_spatial_555555555b87;
        case 0x555555557c4d:
            *num_nodes = 11;
            return native_nodes_spatial_555555557c4d;
        case 0x5555555555d3:
            *num_nodes = 11;
            return native_nodes_spatial_5555555555d3;
        case 0x5555555556db:
            *num_nodes = 11;
            return native_nodes_spatial_5555555556db;
        case 0x5555555556e4:
            *num_nodes = 11;
            return native_nodes_spatial_5555555556e4;
        case 0x5555555556ed:
            *num_nodes = 11;
            return native_nodes_spatial_5555555556ed;
        case 0x7ffff56c62c3:
            *num_nodes = 5;
            return native_nodes_spatial_7ffff56c62c3;
        case 0x555555558513:
            *num_nodes = 5;
            return native_nodes_spatial_555555558513;
        case 0x5555555556f5:
            *num_nodes = 11;
            return native_nodes_spatial_5555555556f5;
        case 0x555555556a63:
            *num_nodes = 7;
            return native_nodes_spatial_555555556a63;
        case 0x555555558aa4:
            *num_nodes = 7;
            return native_nodes_spatial_555555558aa4;
        case 0x5555555555f5:
            *num_nodes = 11;
            return native_nodes_spatial_5555555555f5;
        case 0x555555555b28:
            *num_nodes = 11;
            return native_nodes_spatial_555555555b28;
        case 0x555555555f50:
            *num_nodes = 9;
            return native_nodes_spatial_555555555f50;
        case 0x7ffff56c6268:
            *num_nodes = 5;
            return native_nodes_spatial_7ffff56c6268;
        case 0x8159b108ef:
            *num_nodes = 1;
            return native_nodes_spatial_8159b108ef;
        case 0x5555555584fe:
            *num_nodes = 7;
            return native_nodes_spatial_5555555584fe;
        case 0x555555555fef:
            *num_nodes = 7;
            return native_nodes_spatial_555555555fef;
        case 0x555555555b65:
            *num_nodes = 9;
            return native_nodes_spatial_555555555b65;
        case 0x5555555555e8:
            *num_nodes = 9;
            return native_nodes_spatial_5555555555e8;
        case 0x555555558263:
            *num_nodes = 11;
            return native_nodes_spatial_555555558263;
        case 0x5555555563db:
            *num_nodes = 11;
            return native_nodes_spatial_5555555563db;
        case 0x555555557ee1:
            *num_nodes = 9;
            return native_nodes_spatial_555555557ee1;
        case 0x5555555584d5:
            *num_nodes = 7;
            return native_nodes_spatial_5555555584d5;
        case 0x555555558233:
            *num_nodes = 11;
            return native_nodes_spatial_555555558233;
        case 0x555555556a81:
            *num_nodes = 1;
            return native_nodes_spatial_555555556a81;
        case 0x555555556a20:
            *num_nodes = 11;
            return native_nodes_spatial_555555556a20;
        case 0x555555555b41:
            *num_nodes = 7;
            return native_nodes_spatial_555555555b41;
        case 0x555555557c52:
            *num_nodes = 7;
            return native_nodes_spatial_555555557c52;
        case 0x555555555ff3:
            *num_nodes = 11;
            return native_nodes_spatial_555555555ff3;
        case 0x55555555747c:
            *num_nodes = 11;
            return native_nodes_spatial_55555555747c;
        case 0x555555555800:
            *num_nodes = 11;
            return native_nodes_spatial_555555555800;
        case 0x555555555fae:
            *num_nodes = 11;
            return native_nodes_spatial_555555555fae;
        case 0x555555558313:
            *num_nodes = 11;
            return native_nodes_spatial_555555558313;
        case 0x555555557ee9:
            *num_nodes = 1;
            return native_nodes_spatial_555555557ee9;
        case 0x55555555749c:
            *num_nodes = 11;
            return native_nodes_spatial_55555555749c;
        case 0x555555556422:
            *num_nodes = 7;
            return native_nodes_spatial_555555556422;
        case 0x5555555556e8:
            *num_nodes = 9;
            return native_nodes_spatial_5555555556e8;
        case 0x555555556419:
            *num_nodes = 9;
            return native_nodes_spatial_555555556419;
        case 0x5555555556d6:
            *num_nodes = 11;
            return native_nodes_spatial_5555555556d6;
        case 0x7ffff56c6aa0:
            *num_nodes = 11;
            return native_nodes_spatial_7ffff56c6aa0;
        case 0x5555555582a4:
            *num_nodes = 11;
            return native_nodes_spatial_5555555582a4;
        case 0x5555555556df:
            *num_nodes = 9;
            return native_nodes_spatial_5555555556df;
        case 0x55555555642b:
            *num_nodes = 11;
            return native_nodes_spatial_55555555642b;
        case 0x5555555583a7:
            *num_nodes = 11;
            return native_nodes_spatial_5555555583a7;
        case 0x555555555b5d:
            *num_nodes = 11;
            return native_nodes_spatial_555555555b5d;
        case 0x555555556006:
            *num_nodes = 7;
            return native_nodes_spatial_555555556006;
        case 0x555555555617:
            *num_nodes = 11;
            return native_nodes_spatial_555555555617;
        default:
            *num_nodes = 0;
            return NULL;
    }
}
