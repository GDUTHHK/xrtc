/***************************************************************************
 * 
 * Copyright (c) 2023 str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file histogram.cpp
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/

#include "modules/video_coding/histogram.h"

namespace xrtc {

Histogram::Histogram(size_t num_buckets, size_t max_num_packets) {
    buckets_.resize(num_buckets);
    values_.reserve(max_num_packets);
}

Histogram::~Histogram() {}

void Histogram::Add(size_t value) {
    value = std::min<size_t>(value, buckets_.size() - 1);
    if (index_ < values_.size()) { // 容量已满
        --buckets_[values_[index_]];
        values_[index_] = value;
    } else {
        // 容量还足够
        values_.emplace_back(value);
    }

    ++buckets_[value];
    index_ = (index_ + 1) % values_.capacity();
}

size_t Histogram::InverseCdf(float probability) const {
    size_t bucket = 0;
    float accumulated_probability = 0;
    while (accumulated_probability < probability && bucket < buckets_.size()) {
        accumulated_probability += (float)(buckets_[bucket]) / values_.size();
        ++bucket;
    }

    return bucket;
}

size_t Histogram::NumValues() const {
    return values_.size();
}

} // namespace xrtc


