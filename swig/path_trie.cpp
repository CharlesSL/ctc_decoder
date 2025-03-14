// Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "path_trie.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "decoder_utils.h"

PathTrie::PathTrie() {
  log_prob_b_prev = -NUM_FLT_INF;
  log_prob_nb_prev = -NUM_FLT_INF;
  log_prob_b_cur = -NUM_FLT_INF;
  log_prob_nb_cur = -NUM_FLT_INF;
  score = -NUM_FLT_INF;

  ROOT_ = -1;
  character = ROOT_;
  exists_ = true;
  parent = nullptr;

  dictionary_ = nullptr;
  dictionary_state_ = 0;
  has_dictionary_ = false;

  matcher_ = nullptr;
}

PathTrie::~PathTrie() {
  for (auto child : children_) {
    delete child.second;
  }
}

PathTrie* PathTrie::get_path_trie(int new_char, int time_step, bool reset) {
  auto child = children_.begin();
  for (child = children_.begin(); child != children_.end(); ++child) {
    if (child->first == new_char) {
      break;
    }
  }
  if (child != children_.end()) {
    if (!child->second->exists_) {
      child->second->exists_ = true;
      child->second->log_prob_b_prev = -NUM_FLT_INF;
      child->second->log_prob_nb_prev = -NUM_FLT_INF;
      child->second->log_prob_b_cur = -NUM_FLT_INF;
      child->second->log_prob_nb_cur = -NUM_FLT_INF;
    }
    return (child->second);
  } else {
    if (has_dictionary_) {
      matcher_->SetState(dictionary_state_);
      bool found = matcher_->Find(new_char + 1);
      if (!found) {
        // Adding this character causes word outside dictionary
        auto FSTZERO = fst::TropicalWeight::Zero();
        auto final_weight = dictionary_->Final(dictionary_state_);
        bool is_final = (final_weight != FSTZERO);
        if (is_final && reset) {
          dictionary_state_ = dictionary_->Start();
        }
        return nullptr;
      } else {
        PathTrie* new_path = new PathTrie;
        new_path->character = new_char;
        new_path->time_step = time_step;
        new_path->parent = this;
        new_path->dictionary_ = dictionary_;
        new_path->dictionary_state_ = matcher_->Value().nextstate;
        new_path->has_dictionary_ = true;
        new_path->matcher_ = matcher_;
        children_.push_back(std::make_pair(new_char, new_path));
        return new_path;
      }
    } else {
      PathTrie* new_path = new PathTrie;
      new_path->character = new_char;
      new_path->time_step = time_step;
      new_path->parent = this;
      children_.push_back(std::make_pair(new_char, new_path));
      return new_path;
    }
  }
}

PathTrie* PathTrie::get_path_vec(std::vector<std::pair<int,int>>& output) {
  return get_path_vec(output, ROOT_);
}

PathTrie* PathTrie::get_path_vec(std::vector<std::pair<int,int>>& output, int stop,
                                 size_t max_steps) {
  if (character == stop || character == ROOT_ || output.size() == max_steps) {
    std::reverse(output.begin(), output.end());
    return this;
  } else {
    output.emplace_back(character,time_step);
    return parent->get_path_vec(output, stop, max_steps);
  }
}

void PathTrie::iterate_to_vec_only(std::vector<PathTrie*>& output) {
  if (exists_) {
    output.push_back(this);
  }
  for (auto child : children_) {
    child.second->iterate_to_vec_only(output);
  }
}

void PathTrie::iterate_to_vec(std::vector<PathTrie*>& output) {
  if (exists_) {
    log_prob_b_prev = log_prob_b_cur;
    log_prob_nb_prev = log_prob_nb_cur;

    log_prob_b_cur = -NUM_FLT_INF;
    log_prob_nb_cur = -NUM_FLT_INF;

    score = log_sum_exp(log_prob_b_prev, log_prob_nb_prev);
    output.push_back(this);
  }
  for (auto child : children_) {
    child.second->iterate_to_vec(output);
  }
}

void PathTrie::remove() {
  exists_ = false;

  if (children_.size() == 0) {
    auto child = parent->children_.begin();
    for (child = parent->children_.begin(); child != parent->children_.end();
         ++child) {
      if (child->first == character) {
        parent->children_.erase(child);
        break;
      }
    }

    if (parent->children_.size() == 0 && !parent->exists_) {
      parent->remove();
    }

    delete this;
  }
}

void PathTrie::set_dictionary(fst::StdVectorFst* dictionary) {
  dictionary_ = dictionary;
  dictionary_state_ = dictionary->Start();
  has_dictionary_ = true;
}

using FSTMATCH = fst::SortedMatcher<fst::StdVectorFst>;
void PathTrie::set_matcher(std::shared_ptr<FSTMATCH> matcher) {
  matcher_ = matcher;
}
