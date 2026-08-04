// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace etlfrag::detail {

class HuffmanDecoder {
public:
    HuffmanDecoder();

    int receive(const std::uint8_t* input, int& bitOffset, int maximumBitOffset) const;

private:
    static constexpr int SymbolCount = 256;
    static constexpr int NotYetTransmitted = SymbolCount;
    static constexpr int InternalNode = SymbolCount + 1;

    struct Node {
        Node* left = nullptr;
        Node* right = nullptr;
        Node* parent = nullptr;
        Node* next = nullptr;
        Node* prev = nullptr;
        Node** head = nullptr;
        int weight = 0;
        int symbol = 0;
    };

    struct Tree {
        int nodeCount = 0;
        int pointerCount = 0;
        Node* tree = nullptr;
        Node* listHead = nullptr;
        Node* listTail = nullptr;
        std::array<Node*, SymbolCount + 1> locations{};
        Node** freeList = nullptr;
        std::array<Node, 768> nodes{};
        std::array<Node*, 768> nodePointers{};
    };

    static Node** acquireHead(Tree& tree);
    static void releaseHead(Tree& tree, Node** head);
    static void swapTreeNodes(Tree& tree, Node* first, Node* second);
    static void swapListNodes(Node* first, Node* second);
    static void increment(Tree& tree, Node* node);
    static void addReference(Tree& tree, std::uint8_t symbol);

    Tree decoder_{};
};

} // namespace etlfrag::detail
