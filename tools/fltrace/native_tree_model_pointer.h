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

static const native_tree_node_t native_nodes_pointer_555555555567[11] = {
    {1, 218.0f, 1, 6, 1, 0.0},
    {3, -272.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {3, -65.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 140924.333f, 7, 10, 1, 0.0},
    {5, 0.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555557e9[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555555d3[11] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {2, -124763.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, -124154.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {1, 72.0f, 7, 10, 1, 0.0},
    {2, -138.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555555fef[9] = {
    {2, -14842.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 31732.0f, 3, 6, 1, 0.0},
    {5, -84185.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_8159b108ef[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555555e8[11] = {
    {2, 228459.0f, 1, 4, 1, 0.0},
    {4, 9.38249922e+13f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 1.0f, 5, 8, 1, 0.0},
    {5, -123902.0f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 256409.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555555f5[7] = {
    {2, -32.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {1, 72.0f, 3, 6, 1, 0.0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555558aa4[3] = {
    {4, 9.38249922e+13f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_555555555fe5[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_555555558aa7[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_555555556a20[11] = {
    {1, 255.0f, 1, 4, 1, 0.0},
    {2, 124807.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, 245.0f, 5, 8, 1, 0.0},
    {4, 1.17281152e+14f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 124807.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555556006[11] = {
    {5, 0.0f, 1, 6, 1, 0.0},
    {1, 84.0f, 2, 5, 1, 0.0},
    {3, -422.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 10, 1, 0.0},
    {5, 1.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555556411[7] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {3, 228.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -4722.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555555ff3[11] = {
    {3, -343.0f, 1, 6, 1, 0.0},
    {2, 188.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 123960.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {1, 86.0f, 7, 10, 1, 0.0},
    {3, -175.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555555b82[7] = {
    {1, 85.0f, 1, 6, 1, 0.0},
    {4, 9.38249922e+13f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 0.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555555ca[9] = {
    {2, 659.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 808.0f, 3, 6, 1, 0.0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_55555555560a[11] = {
    {1, 192.0f, 1, 4, 1, 0.0},
    {2, 240751.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, 255211.0f, 5, 8, 1, 0.0},
    {4, 9.38249922e+13f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -126293.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555555980[1] = {
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555563ef[7] = {
    {4, 9.38249922e+13f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 123988.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 0.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_55555555747c[11] = {
    {2, 231967.667f, 1, 4, 1, 0.0},
    {5, -262.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 1.0f, 5, 8, 1, 0.0},
    {2, 246340.333f, 6, 7, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_555555555c8a[7] = {
    {2, -224295.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {1, 341.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, 340.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555555b59[7] = {
    {4, 9.38249922e+13f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {3, 10.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -29759.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555555ec[7] = {
    {2, 662.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 645.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {5, 1518.0f, 5, 6, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555557f4[7] = {
    {2, -153141.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {1, 85.0f, 3, 6, 1, 0.0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555563bd[11] = {
    {3, 85.0f, 1, 6, 1, 0.0},
    {1, 85.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 1.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {2, -127656.0f, 7, 8, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 1.0f, 9, 10, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
};

static const native_tree_node_t native_nodes_pointer_5555555556d3[7] = {
    {1, 400.0f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, -107085.5f, 3, 6, 1, 0.0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555557483[11] = {
    {3, -83.0f, 1, 6, 1, 0.0},
    {2, 242051.333f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 251441.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {2, 256831.333f, 7, 10, 1, 0.0},
    {5, -65.0f, 8, 9, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555556ed[7] = {
    {2, -72707.6667f, 1, 6, 1, 0.0},
    {1, 429.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, 5761.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_55555555605b[7] = {
    {4, 9.38249922e+13f, 1, 2, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {1, 26.0f, 3, 6, 1, 0.0},
    {5, 0.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555565a4[7] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {1, 170.0f, 2, 5, 1, 0.0},
    {3, 92.0f, 3, 4, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_555555555617[7] = {
    {4, 9.38249922e+13f, 1, 6, 1, 0.0},
    {4, 9.38249922e+13f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {5, -1105.0f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t native_nodes_pointer_5555555556db[7] = {
    {2, -73456.6667f, 1, 6, 1, 0.0},
    {1, 426.0f, 2, 3, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {4, 9.38249922e+13f, 4, 5, 1, 0.0},
    {-1, 0.0f, 0, 0, 0, 0},
    {-1, 0.0f, 0, 0, 0, 1},
    {-1, 0.0f, 0, 0, 0, 0},
};

static const native_tree_node_t* get_native_nodes(uint64_t pc, int* num_nodes) {
    switch (pc) {
        case 0x555555555567:
            *num_nodes = 11;
            return native_nodes_pointer_555555555567;
        case 0x5555555557e9:
            *num_nodes = 1;
            return native_nodes_pointer_5555555557e9;
        case 0x5555555555d3:
            *num_nodes = 11;
            return native_nodes_pointer_5555555555d3;
        case 0x555555555fef:
            *num_nodes = 9;
            return native_nodes_pointer_555555555fef;
        case 0x8159b108ef:
            *num_nodes = 1;
            return native_nodes_pointer_8159b108ef;
        case 0x5555555555e8:
            *num_nodes = 11;
            return native_nodes_pointer_5555555555e8;
        case 0x5555555555f5:
            *num_nodes = 7;
            return native_nodes_pointer_5555555555f5;
        case 0x555555558aa4:
            *num_nodes = 3;
            return native_nodes_pointer_555555558aa4;
        case 0x555555555fe5:
            *num_nodes = 1;
            return native_nodes_pointer_555555555fe5;
        case 0x555555558aa7:
            *num_nodes = 1;
            return native_nodes_pointer_555555558aa7;
        case 0x555555556a20:
            *num_nodes = 11;
            return native_nodes_pointer_555555556a20;
        case 0x555555556006:
            *num_nodes = 11;
            return native_nodes_pointer_555555556006;
        case 0x555555556411:
            *num_nodes = 7;
            return native_nodes_pointer_555555556411;
        case 0x555555555ff3:
            *num_nodes = 11;
            return native_nodes_pointer_555555555ff3;
        case 0x555555555b82:
            *num_nodes = 7;
            return native_nodes_pointer_555555555b82;
        case 0x5555555555ca:
            *num_nodes = 9;
            return native_nodes_pointer_5555555555ca;
        case 0x55555555560a:
            *num_nodes = 11;
            return native_nodes_pointer_55555555560a;
        case 0x555555555980:
            *num_nodes = 1;
            return native_nodes_pointer_555555555980;
        case 0x5555555563ef:
            *num_nodes = 7;
            return native_nodes_pointer_5555555563ef;
        case 0x55555555747c:
            *num_nodes = 11;
            return native_nodes_pointer_55555555747c;
        case 0x555555555c8a:
            *num_nodes = 7;
            return native_nodes_pointer_555555555c8a;
        case 0x555555555b59:
            *num_nodes = 7;
            return native_nodes_pointer_555555555b59;
        case 0x5555555555ec:
            *num_nodes = 7;
            return native_nodes_pointer_5555555555ec;
        case 0x5555555557f4:
            *num_nodes = 7;
            return native_nodes_pointer_5555555557f4;
        case 0x5555555563bd:
            *num_nodes = 11;
            return native_nodes_pointer_5555555563bd;
        case 0x5555555556d3:
            *num_nodes = 7;
            return native_nodes_pointer_5555555556d3;
        case 0x555555557483:
            *num_nodes = 11;
            return native_nodes_pointer_555555557483;
        case 0x5555555556ed:
            *num_nodes = 7;
            return native_nodes_pointer_5555555556ed;
        case 0x55555555605b:
            *num_nodes = 7;
            return native_nodes_pointer_55555555605b;
        case 0x5555555565a4:
            *num_nodes = 7;
            return native_nodes_pointer_5555555565a4;
        case 0x555555555617:
            *num_nodes = 7;
            return native_nodes_pointer_555555555617;
        case 0x5555555556db:
            *num_nodes = 7;
            return native_nodes_pointer_5555555556db;
        default:
            *num_nodes = 0;
            return NULL;
    }
}
