#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

template <std::unsigned_integral Key = std::size_t, class T = void *> class offset_map {
public:
    using key_type    = Key;
    using mapped_type = T;
    using size_type   = std::size_t;

private:
    using slot_type = std::optional<mapped_type>;

    template <bool Const>
    struct entry_ref {
        const key_type first;
        std::conditional_t<Const, const mapped_type&, mapped_type&> second;
    };

public:
    template <bool Const>
    class basic_iterator {
        friend class offset_map;
        template <bool>
        friend class basic_iterator;

        using parent_type = std::conditional_t<Const, const offset_map, offset_map>;

        parent_type* parent_ = nullptr;
        size_type index_ = 0;
        mutable std::optional<entry_ref<Const>> cache_;

        basic_iterator(parent_type* parent, size_type index) noexcept
            : parent_(parent), index_(index) {
            skip_empty();
        }

        void skip_empty() noexcept {
            while (parent_ && index_ < parent_->slots_.size() && !parent_->slots_[index_]) {
                ++index_;
            }
        }

    public:
        using difference_type   = std::ptrdiff_t;
        using value_type        = std::pair<key_type, mapped_type>;
        using reference         = entry_ref<Const>;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept  = std::input_iterator_tag;

        basic_iterator() = default;

        basic_iterator(const basic_iterator<false>& other) requires Const
            : parent_(other.parent_), index_(other.index_) {}

        [[nodiscard]] reference operator*() const noexcept {
            return {
                static_cast<key_type>(parent_->base_ + index_),
                *parent_->slots_[index_]
            };
        }

        [[nodiscard]] const reference* operator->() const noexcept {
            cache_.emplace(**this);
            return &*cache_;
        }

        basic_iterator& operator++() noexcept {
            ++index_;
            skip_empty();
            return *this;
        }

        basic_iterator operator++(int) noexcept {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const basic_iterator& a,
                               const basic_iterator& b) noexcept {
            return a.parent_ == b.parent_ && a.index_ == b.index_;
        }
    };

    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    offset_map() = default;

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }

    // Number of stored slots in the underlying span, including holes.
    [[nodiscard]] size_type key_span() const noexcept { return slots_.size(); }

    [[nodiscard]] std::optional<key_type> min_key() const noexcept {
        if (empty()) return std::nullopt;
        return base_;
    }

    [[nodiscard]] std::optional<key_type> max_key() const noexcept {
        if (empty()) return std::nullopt;
        return static_cast<key_type>(base_ + slots_.size() - 1);
    }

    void clear() noexcept {
        slots_.clear();
        base_ = 0;
        size_ = 0;
    }

    void shrink_to_fit() {
        slots_.shrink_to_fit();
    }

    [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, slots_.size()); }

    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, slots_.size()); }

    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] iterator find(key_type key) noexcept {
        if (!in_span(key)) return end();
        const auto idx = index_of(key);
        return slots_[idx] ? iterator(this, idx) : end();
    }

    [[nodiscard]] const_iterator find(key_type key) const noexcept {
        if (!in_span(key)) return end();
        const auto idx = index_of(key);
        return slots_[idx] ? const_iterator(this, idx) : end();
    }

    [[nodiscard]] bool contains(key_type key) const noexcept {
        return find(key) != end();
    }

    [[nodiscard]] mapped_type* get(key_type key) noexcept {
        if (!in_span(key)) return nullptr;
        auto& slot = slots_[index_of(key)];
        return slot ? std::addressof(*slot) : nullptr;
    }

    [[nodiscard]] const mapped_type* get(key_type key) const noexcept {
        if (!in_span(key)) return nullptr;
        const auto& slot = slots_[index_of(key)];
        return slot ? std::addressof(*slot) : nullptr;
    }

    mapped_type& at(key_type key) {
        if (auto* p = get(key)) return *p;
        throw std::out_of_range("offset_map::at: key not found");
    }

    const mapped_type& at(key_type key) const {
        if (auto* p = get(key)) return *p;
        throw std::out_of_range("offset_map::at: key not found");
    }

    mapped_type& operator[](key_type key)
        requires std::default_initializable<mapped_type>
    {
        ensure_slot(key);
        auto& slot = slots_[index_of(key)];
        if (!slot) {
            ++size_;
            return slot.emplace();
        }
        return *slot;
    }

    template <class U>
    std::pair<iterator, bool> insert(key_type key, U&& value) {
        ensure_slot(key);
        const auto idx = index_of(key);
        auto& slot = slots_[idx];

        if (slot) {
            return { iterator(this, idx), false };
        }

        slot.emplace(std::forward<U>(value));
        ++size_;
        return { iterator(this, idx), true };
    }

    template <class... Args>
    std::pair<iterator, bool> try_emplace(key_type key, Args&&... args) {
        ensure_slot(key);
        const auto idx = index_of(key);
        auto& slot = slots_[idx];

        if (slot) {
            return { iterator(this, idx), false };
        }

        slot.emplace(std::forward<Args>(args)...);
        ++size_;
        return { iterator(this, idx), true };
    }

    template <class U>
    std::pair<iterator, bool> insert_or_assign(key_type key, U&& value) {
        ensure_slot(key);
        const auto idx = index_of(key);
        auto& slot = slots_[idx];

        const bool inserted = !slot.has_value();
        if (inserted) {
            slot.emplace(std::forward<U>(value));
            ++size_;
        } else {
            *slot = std::forward<U>(value);
        }

        return { iterator(this, idx), inserted };
    }

    size_type erase(key_type key) {
        if (!in_span(key)) return 0;

        auto& slot = slots_[index_of(key)];
        if (!slot) return 0;

        slot.reset();
        --size_;
        trim();
        return 1;
    }

private:
    std::vector<slot_type> slots_;
    key_type base_ = 0;
    size_type size_ = 0;

    [[nodiscard]] bool in_span(key_type key) const noexcept {
        return !slots_.empty()
            && key >= base_
            && static_cast<size_type>(key - base_) < slots_.size();
    }

    [[nodiscard]] size_type index_of(key_type key) const noexcept {
        return static_cast<size_type>(key - base_);
    }

    void ensure_slot(key_type key) {
        if (slots_.empty()) {
            base_ = key;
            slots_.resize(1);
            return;
        }

        if (key < base_) {
            const size_type prepend = static_cast<size_type>(base_ - key);
            slots_.insert(slots_.begin(), prepend, std::nullopt);
            base_ = key;
            return;
        }

        const size_type idx = index_of(key);
        if (idx >= slots_.size()) {
            slots_.resize(idx + 1);
        }
    }

    void trim() {
        if (slots_.empty()) return;

        if (size_ == 0) {
            clear();
            return;
        }

        size_type first = 0;
        while (first < slots_.size() && !slots_[first]) {
            ++first;
        }

        size_type last = slots_.size();
        while (last > first && !slots_[last - 1]) {
            --last;
        }

        if (last < slots_.size()) {
            slots_.resize(last);
        }

        if (first > 0) {
            slots_.erase(slots_.begin(), slots_.begin() + static_cast<std::ptrdiff_t>(first));
            base_ = static_cast<key_type>(base_ + first);
        }
    }
};
