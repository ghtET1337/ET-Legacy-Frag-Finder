/*
 * The Huffman tree construction follows the GPLv3 ET: Legacy implementation
 * in src/qcommon/huffman.c and uses the protocol frequency table from
 * src/qcommon/msg.c. The compact decoder below keeps only the operations that
 * are required while reading client demo messages.
 */

// SPDX-License-Identifier: GPL-3.0-or-later
#include "idtech3_huffman.hpp"

#include <array>
#include <stdexcept>

namespace etlfrag::detail {
namespace {

constexpr std::array<int, 256> kProtocolFrequencies = {
    250315, 41193, 6292, 7106, 3730, 3750, 6110, 23283,
    33317, 6950, 7838, 9714, 9257, 17259, 3949, 1778,
    8288, 1604, 1590, 1663, 1100, 1213, 1238, 1134,
    1749, 1059, 1246, 1149, 1273, 4486, 2805, 3472,
    21819, 1159, 1670, 1066, 1043, 1012, 1053, 1070,
    1726, 888, 1180, 850, 960, 780, 1752, 3296,
    10630, 4514, 5881, 2685, 4650, 3837, 2093, 1867,
    2584, 1949, 1972, 940, 1134, 1788, 1670, 1206,
    5719, 6128, 7222, 6654, 3710, 3795, 1492, 1524,
    2215, 1140, 1355, 971, 2180, 1248, 1328, 1195,
    1770, 1078, 1264, 1266, 1168, 965, 1155, 1186,
    1347, 1228, 1529, 1600, 2617, 2048, 2546, 3275,
    2410, 3585, 2504, 2800, 2675, 6146, 3663, 2840,
    14253, 3164, 2221, 1687, 3208, 2739, 3512, 4796,
    4091, 3515, 5288, 4016, 7937, 6031, 5360, 3924,
    4892, 3743, 4566, 4807, 5852, 6400, 6225, 8291,
    23243, 7838, 7073, 8935, 5437, 4483, 3641, 5256,
    5312, 5328, 5370, 3492, 2458, 1694, 1821, 2121,
    1916, 1149, 1516, 1367, 1236, 1029, 1258, 1104,
    1245, 1006, 1149, 1025, 1241, 952, 1287, 997,
    1713, 1009, 1187, 879, 1099, 929, 1078, 951,
    1656, 930, 1153, 1030, 1262, 1062, 1214, 1060,
    1621, 930, 1106, 912, 1034, 892, 1158, 990,
    1175, 850, 1121, 903, 1087, 920, 1144, 1056,
    3462, 2240, 4397, 12136, 7758, 1345, 1307, 3278,
    1950, 886, 1023, 1112, 1077, 1042, 1061, 1071,
    1484, 1001, 1096, 915, 1052, 995, 1070, 876,
    1111, 851, 1059, 805, 1112, 923, 1103, 817,
    1899, 1872, 976, 841, 1127, 956, 1159, 950,
    7791, 954, 1289, 933, 1127, 3207, 1020, 927,
    1355, 768, 1040, 745, 952, 805, 1073, 740,
    1013, 805, 1008, 796, 996, 1057, 11457, 13504,
};

} // namespace

HuffmanDecoder::Node** HuffmanDecoder::acquireHead(Tree& tree) {
    if (tree.freeList == nullptr) {
        if (tree.pointerCount >= static_cast<int>(tree.nodePointers.size())) {
            throw std::runtime_error("Huffman pointer pool exhausted");
        }
        return &tree.nodePointers[static_cast<std::size_t>(tree.pointerCount++)];
    }

    Node** result = tree.freeList;
    tree.freeList = reinterpret_cast<Node**>(*result);
    return result;
}

void HuffmanDecoder::releaseHead(Tree& tree, Node** head) {
    *head = reinterpret_cast<Node*>(tree.freeList);
    tree.freeList = head;
}

void HuffmanDecoder::swapTreeNodes(Tree& tree, Node* first, Node* second) {
    Node* firstParent = first->parent;
    Node* secondParent = second->parent;

    if (firstParent != nullptr) {
        if (firstParent->left == first) {
            firstParent->left = second;
        } else {
            firstParent->right = second;
        }
    } else {
        tree.tree = second;
    }

    if (secondParent != nullptr) {
        if (secondParent->left == second) {
            secondParent->left = first;
        } else {
            secondParent->right = first;
        }
    } else {
        tree.tree = first;
    }

    first->parent = secondParent;
    second->parent = firstParent;
}

void HuffmanDecoder::swapListNodes(Node* first, Node* second) {
    Node* temporary = first->next;
    first->next = second->next;
    second->next = temporary;

    temporary = first->prev;
    first->prev = second->prev;
    second->prev = temporary;

    if (first->next == first) {
        first->next = second;
    }
    if (second->next == second) {
        second->next = first;
    }
    if (first->next != nullptr) {
        first->next->prev = first;
    }
    if (second->next != nullptr) {
        second->next->prev = second;
    }
    if (first->prev != nullptr) {
        first->prev->next = first;
    }
    if (second->prev != nullptr) {
        second->prev->next = second;
    }
}

void HuffmanDecoder::increment(Tree& tree, Node* node) {
    if (node == nullptr) {
        return;
    }

    if (node->next != nullptr && node->next->weight == node->weight) {
        Node* leader = *node->head;
        if (leader != node->parent) {
            swapTreeNodes(tree, leader, node);
        }
        swapListNodes(leader, node);
    }

    if (node->prev != nullptr && node->prev->weight == node->weight) {
        *node->head = node->prev;
    } else {
        *node->head = nullptr;
        releaseHead(tree, node->head);
    }

    ++node->weight;
    if (node->next != nullptr && node->next->weight == node->weight) {
        node->head = node->next->head;
    } else {
        node->head = acquireHead(tree);
        *node->head = node;
    }

    if (node->parent != nullptr) {
        increment(tree, node->parent);
        if (node->prev == node->parent) {
            swapListNodes(node, node->parent);
            if (*node->head == node) {
                *node->head = node->parent;
            }
        }
    }
}

void HuffmanDecoder::addReference(Tree& tree, std::uint8_t symbol) {
    if (tree.locations[symbol] == nullptr) {
        if (tree.nodeCount + 2 > static_cast<int>(tree.nodes.size())) {
            throw std::runtime_error("Huffman node pool exhausted");
        }

        Node* leaf = &tree.nodes[static_cast<std::size_t>(tree.nodeCount++)];
        Node* internal = &tree.nodes[static_cast<std::size_t>(tree.nodeCount++)];

        internal->symbol = InternalNode;
        internal->weight = 1;
        internal->next = tree.listHead->next;
        if (tree.listHead->next != nullptr) {
            tree.listHead->next->prev = internal;
            if (tree.listHead->next->weight == 1) {
                internal->head = tree.listHead->next->head;
            } else {
                internal->head = acquireHead(tree);
                *internal->head = internal;
            }
        } else {
            internal->head = acquireHead(tree);
            *internal->head = internal;
        }
        tree.listHead->next = internal;
        internal->prev = tree.listHead;

        leaf->symbol = symbol;
        leaf->weight = 1;
        leaf->next = tree.listHead->next;
        if (tree.listHead->next != nullptr) {
            tree.listHead->next->prev = leaf;
            if (tree.listHead->next->weight == 1) {
                leaf->head = tree.listHead->next->head;
            } else {
                leaf->head = acquireHead(tree);
                *leaf->head = internal;
            }
        } else {
            leaf->head = acquireHead(tree);
            *leaf->head = leaf;
        }
        tree.listHead->next = leaf;
        leaf->prev = tree.listHead;
        leaf->left = nullptr;
        leaf->right = nullptr;

        if (tree.listHead->parent != nullptr) {
            if (tree.listHead->parent->left == tree.listHead) {
                tree.listHead->parent->left = internal;
            } else {
                tree.listHead->parent->right = internal;
            }
        } else {
            tree.tree = internal;
        }

        internal->right = leaf;
        internal->left = tree.listHead;
        internal->parent = tree.listHead->parent;
        tree.listHead->parent = internal;
        leaf->parent = internal;
        tree.locations[symbol] = leaf;

        increment(tree, internal->parent);
    } else {
        increment(tree, tree.locations[symbol]);
    }
}

HuffmanDecoder::HuffmanDecoder() {
    Node* root = &decoder_.nodes[static_cast<std::size_t>(decoder_.nodeCount++)];
    decoder_.tree = root;
    decoder_.listHead = root;
    decoder_.listTail = root;
    decoder_.locations[NotYetTransmitted] = root;
    root->symbol = NotYetTransmitted;
    root->weight = 0;

    for (std::size_t symbol = 0; symbol < kProtocolFrequencies.size(); ++symbol) {
        for (int i = 0; i < kProtocolFrequencies[symbol]; ++i) {
            addReference(decoder_, static_cast<std::uint8_t>(symbol));
        }
    }
}

int HuffmanDecoder::receive(
    const std::uint8_t* input,
    int& bitOffset,
    int maximumBitOffset) const {
    Node* node = decoder_.tree;
    while (node != nullptr && node->symbol == InternalNode) {
        if (bitOffset >= maximumBitOffset) {
            bitOffset = maximumBitOffset + 1;
            return 0;
        }
        const int bit = (input[bitOffset >> 3] >> (bitOffset & 7)) & 1;
        ++bitOffset;
        node = bit != 0 ? node->right : node->left;
    }

    if (node == nullptr) {
        throw std::runtime_error("Invalid Huffman code in demo message");
    }
    return node->symbol;
}

} // namespace etlfrag::detail
