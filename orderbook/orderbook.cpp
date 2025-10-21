#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <iostream>
#include <algorithm>

struct Order {
    uint64_t order_id;     // Unique order identifier
    bool is_buy;           // true = buy, false = sell
    double price;          // Limit price
    uint64_t quantity;     // Remaining quantity
    uint64_t timestamp_ns; // Order entry timestamp in nanoseconds
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};

class MemoryPool {
private:
    static constexpr size_t BLOCK_SIZE = 1024;
    std::vector<Node*> blocks;
    std::deque<Node*> free_list;

    void allocate_block() {
        Node* block = static_cast<Node*>(operator new(sizeof(Node) * BLOCK_SIZE));
        blocks.push_back(block);
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            free_list.push_back(&block[i]);
        }
    }

public:
    MemoryPool() = default;
    ~MemoryPool() {
        for (auto block : blocks) {
            operator delete(block);
        }
    }

    Node* allocate() {
        if (free_list.empty()) {
            allocate_block();
        }
        Node* ptr = free_list.front();
        free_list.pop_front();
        return ptr;
    }

    void deallocate(Node* ptr) {
        ptr->~Node();
        free_list.push_back(ptr);
    }
};

class OrderBook {
private:
    struct Node : public Order {
        Node* next = nullptr;
        Node* prev = nullptr;

        Node() = default;
        Node(const Order& o) : Order(o) {}
    };

    struct PriceLevelData {
        uint64_t total_quantity = 0;
        Node* head = nullptr;
        Node* tail = nullptr;
    };

    std::map<double, PriceLevelData, std::greater<double>> bids;
    std::map<double, PriceLevelData> asks;
    std::unordered_map<uint64_t, Node*> order_lookup;
    MemoryPool pool;

public:
    void add_order(const Order& order) {
        Node* node = pool.allocate();
        new (node) Node(order);
        order_lookup[order.order_id] = node;

        auto& side = order.is_buy ? bids : asks;
        auto& level = side[order.price];

        level.total_quantity += order.quantity;
        node->next = nullptr;
        node->prev = level.tail;
        if (level.tail) {
            level.tail->next = node;
        } else {
            level.head = node;
        }
        level.tail = node;
    }

    bool cancel_order(uint64_t order_id) {
        auto it = order_lookup.find(order_id);
        if (it == order_lookup.end()) {
            return false;
        }
        Node* node = it->second;

        auto& side = node->is_buy ? bids : asks;
        auto level_it = side.find(node->price);
        if (level_it == side.end()) {
            return false;
        }
        auto& level = level_it->second;

        level.total_quantity -= node->quantity;

        if (node->prev) {
            node->prev->next = node->next;
        } else {
            level.head = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            level.tail = node->prev;
        }

        if (level.head == nullptr) {
            side.erase(level_it);
        }

        order_lookup.erase(it);
        pool.deallocate(node);
        return true;
    }

    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
        auto it = order_lookup.find(order_id);
        if (it == order_lookup.end()) {
            return false;
        }
        Node* node = it->second;
        double old_price = node->price;
        uint64_t old_quantity = node->quantity;

        if (new_quantity == 0) {
            return cancel_order(order_id);
        }

        auto& side = node->is_buy ? bids : asks;

        if (new_price != old_price) {
            // Remove from old level
            auto old_level_it = side.find(old_price);
            if (old_level_it == side.end()) {
                return false;
            }
            auto& old_level = old_level_it->second;

            old_level.total_quantity -= old_quantity;

            if (node->prev) {
                node->prev->next = node->next;
            } else {
                old_level.head = node->next;
            }
            if (node->next) {
                node->next->prev = node->prev;
            } else {
                old_level.tail = node->prev;
            }

            if (old_level.head == nullptr) {
                side.erase(old_level_it);
            }

            // Update node
            node->price = new_price;
            node->quantity = new_quantity;

            // Add to new level
            auto& new_level = side[new_price];

            new_level.total_quantity += new_quantity;
            node->next = nullptr;
            node->prev = new_level.tail;
            if (new_level.tail) {
                new_level.tail->next = node;
            } else {
                new_level.head = node;
            }
            new_level.tail = node;
        } else {
            // Same price, update quantity
            auto level_it = side.find(old_price);
            if (level_it == side.end()) {
                return false;
            }
            auto& level = level_it->second;

            level.total_quantity -= old_quantity;
            level.total_quantity += new_quantity;
            node->quantity = new_quantity;
        }

        return true;
    }

    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids_out, std::vector<PriceLevel>& asks_out) const {
        bids_out.clear();
        asks_out.clear();

        size_t count = 0;
        for (const auto& [price, level] : bids) {
            if (count >= depth) break;
            bids_out.push_back({price, level.total_quantity});
            ++count;
        }

        count = 0;
        for (const auto& [price, level] : asks) {
            if (count >= depth) break;
            asks_out.push_back({price, level.total_quantity});
            ++count;
        }
    }

    void print_book(size_t depth = 10) const {
        std::vector<PriceLevel> b, a;
        get_snapshot(depth, b, a);

        std::cout << "Bids:" << std::endl;
        for (const auto& lvl : b) {
            std::cout << lvl.price << " : " << lvl.total_quantity << std::endl;
        }

        std::cout << "Asks:" << std::endl;
        for (const auto& lvl : a) {
            std::cout << lvl.price << " : " << lvl.total_quantity << std::endl;
        }
    }
};