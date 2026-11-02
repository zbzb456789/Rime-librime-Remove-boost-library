//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-12-12 GONG Chen <chen.sst@gmail.com>
//
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/translation.h>
#include <rime/gear/uniquifier.h>

namespace rime {

class UniquifiedTranslation : public CacheTranslation {
 public:
  UniquifiedTranslation(an<Translation> translation, CandidateList* candidates)
      : CacheTranslation(translation), candidates_(candidates) {
    SyncIndex();
    Uniquify();
  }
  virtual bool Next();

 protected:
  void SyncIndex();
  bool Uniquify();

  an<Translation> translation_;
  CandidateList* candidates_;
  hash_map<string, size_t> text_index_;
  size_t indexed_count_ = 0;
};

bool UniquifiedTranslation::Next() {
  return CacheTranslation::Next() && Uniquify();
}

bool UniquifiedTranslation::Uniquify() {
  while (!exhausted()) {
    SyncIndex();
    auto next = Peek();
    if (!next) {
      // The upstream chain can report itself as non-exhausted while a filter
      // has no candidate left to expose. Treat that as the end of this
      // translation instead of dereferencing an empty candidate.
      set_exhausted(true);
      break;
    }
    auto found = text_index_.find(next->text());
    if (found == text_index_.end() || found->second >= candidates_->size()) {
      // Encountered a unique candidate.
      return true;
    }
    CandidateList::iterator previous = candidates_->begin() + found->second;
    auto uniquified = As<UniquifiedCandidate>(*previous);
    if (!uniquified) {
      *previous = uniquified =
          New<UniquifiedCandidate>(*previous, "uniquified");
    }
    uniquified->Append(next);
    CacheTranslation::Next();
  }
  return false;
}

void UniquifiedTranslation::SyncIndex() {
  if (!candidates_)
    return;
  for (size_t i = indexed_count_; i < candidates_->size(); ++i) {
    const auto& cand = (*candidates_)[i];
    if (cand) {
      text_index_.try_emplace(cand->text(), i);
    }
  }
  indexed_count_ = candidates_->size();
}

// Uniquifier

Uniquifier::Uniquifier(const Ticket& ticket) : Filter(ticket) {}

an<Translation> Uniquifier::Apply(an<Translation> translation,
                                  CandidateList* candidates) {
  return New<UniquifiedTranslation>(translation, candidates);
}

}  // namespace rime
