/*
 * Copyright (C) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/debug_helpers.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace NEO {

template <typename ValueType, class Compare>
class BaseSortedPointerWithValueVector {
  public:
    using PointerPair = std::pair<const void *, std::unique_ptr<ValueType>>;
    using Container = std::vector<PointerPair>;

    BaseSortedPointerWithValueVector() : compareFunctor(Compare()){};

    void insert(const void *ptr, const ValueType &value) {
        allocations.push_back(std::make_pair(ptr, std::make_unique<ValueType>(value)));
        for (size_t i = allocations.size() - 1; i > 0; --i) {
            if (allocations[i].first < allocations[i - 1].first) {
                std::iter_swap(allocations.begin() + i, allocations.begin() + i - 1);
            } else {
                break;
            }
        }
    }

    void remove(const void *ptr) {
        auto removeIt = std::remove_if(allocations.begin(), allocations.end(), [&ptr](const PointerPair &other) {
            return ptr == other.first;
        });
        allocations.erase(removeIt);
    }

    typename Container::iterator getImpl(const void *ptr) {
        if (allocations.size() == 0) {
            return allocations.end();
        }

        if (nullptr == ptr) {
            return allocations.end();
        }

        DEBUG_BREAK_IF(allocations.size() > static_cast<size_t>(std::numeric_limits<int>::max()));

        int begin = 0;
        int end = static_cast<int>(allocations.size() - 1);
        while (end >= begin) {
            int currentPos = (begin + end) / 2;
            const auto &allocation = allocations[currentPos];

            if (compareFunctor(allocation.second, ptr, allocation.first)) {
                return allocations.begin() + currentPos;
            } else if (ptr < allocation.first) {
                end = currentPos - 1;
                continue;
            } else {
                begin = currentPos + 1;
                continue;
            }
        }
        return allocations.end();
    }

    std::unique_ptr<ValueType> extract(const void *ptr) {
        std::unique_ptr<ValueType> retVal{};
        auto it = getImpl(ptr);
        if (it != allocations.end()) {
            retVal.swap(it->second);
            allocations.erase(it);
        }
        return retVal;
    }

    ValueType *get(const void *ptr) {
        auto it = getImpl(ptr);
        if (it != allocations.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    size_t getNumAllocs() const { return allocations.size(); }

    Container allocations;
    Compare compareFunctor;
};
} // namespace NEO