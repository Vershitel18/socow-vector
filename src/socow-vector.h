#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace ct {

template <typename T, std::size_t SMALL_SIZE>
class SocowVector {
  // static_assert(std::is_copy_constructible_v<T>, "T must have a copy constructor");
  // static_assert(std::is_nothrow_move_constructible_v<T>, "T must have a non-throwing move constructor");
  // static_assert(std::is_copy_assignable_v<T>, "T must have a copy assignment operator");
  // static_assert(std::is_nothrow_move_assignable_v<T>, "T must have a non-throwing move assignment operator");
  // static_assert(std::is_nothrow_swappable_v<T>, "T must have a non-throwing swap");

  static_assert(SMALL_SIZE > 0, "SMALL_SIZE must be positive");

  struct Buffer {
    std::size_t capacity;
    std::size_t ref_count;

    T data_[0];
  };

  std::size_t size_;
  bool is_big;

  union {
    T small_[SMALL_SIZE];
    Buffer* big_;
  };

public:
  using ValueType = T;

  using Reference = T&;
  using ConstReference = const T&;

  using Pointer = T*;
  using ConstPointer = const T*;

  using Iterator = Pointer;
  using ConstIterator = ConstPointer;

  // nothrow
  SocowVector()
      : size_{0}
      , is_big(false) {}

  // Strong garanty
  SocowVector(const SocowVector& other) {
    if (!other.is_small()) {
      big_ = other.big_;
      ref_plus();
      is_big = true;
    } else {
      std::uninitialized_copy_n(other.raw_data(), other.size(), small_);
      is_big = false;
    }
    size_ = other.size_;
  }

  // nothrow
  SocowVector(SocowVector&& other) noexcept {
    if (!other.is_small()) {
      big_ = other.big_;
      size_ = other.size_;
      is_big = true;

      other.big_ = nullptr;
      other.size_ = 0;
      other.is_big = false;
    } else {
      std::uninitialized_move_n(other.raw_data(), other.size_, small_);
      size_ = other.size_;
      is_big = false;
    }
  }

  // strong garanty
  SocowVector& operator=(const SocowVector& other) {
    if (this != &other) {
      if (other.is_small()) {
        SocowVector tmp(other.size());
        std::uninitialized_copy_n(other.raw_data(), other.size(), tmp.raw_data());
        tmp.size_ = other.size();

        clear_data();
        swap(tmp);
      } else {
        clear_data();
        big_ = other.big_;
        ref_plus();
        size_ = other.size_;
        is_big = true;
      }
    }
    return *this;
  }

  // nothrow
  SocowVector& operator=(SocowVector&& other) noexcept {
    if (this != &other) {
      clear_data();
      swap(other);
    }
    return *this;
  }

  // nothrow
  ~SocowVector() {
    // if (!is_small(*this)) {
    //   release_ref();
    //   big_ = nullptr;
    // } else {
    //   for (std::size_t i = size_; i > 0; --i) {
    //     small_[i - 1].~T();
    //   }
    // }
    // size_ = 0;
    clear_data();
  }

  // strong garanty
  void detauch() {
    if (!is_small() && this->big_->ref_count > 1) {
      Buffer* buffer = static_cast<Buffer*>(operator new(
          sizeof(Buffer) + (big_->capacity * sizeof(T)),
          std::align_val_t(alignof(T))
      ));
      buffer->capacity = big_->capacity;
      buffer->ref_count = 1;
      try {
        std::uninitialized_copy_n(raw_data(), size(), buffer->data_);
      } catch (...) {
        operator delete(buffer, std::align_val_t(alignof(T)));
        throw;
      }
      release_ref();
      big_ = buffer;
    }
  }

  void destrou_small() {
    for (std::size_t i = 0; i < size(); ++i) {
      small_[i].~T(); // nothrow
    }
  }

  // nothrow
  void swap(SocowVector& other) noexcept {
    if (this == &other) {
      return;
    }
    using std::swap;
    if (is_small() && other.is_small()) {
      std::size_t index = 0;
      for (; index < std::min(this->size_, other.size_); ++index) {
        swap(small_[index], other.small_[index]); // nothrow
      }
      SocowVector* small_vector = this->size_ < other.size_ ? this : &other;
      SocowVector* big_vector = this->size_ < other.size_ ? &other : this;
      std::uninitialized_move_n(
          big_vector->raw_data() + index,
          big_vector->size() - index,
          small_vector->raw_data() + index
      );
      for (; index < big_vector->size_; ++index) {
        big_vector->small_[index].~T();
      }
      swap(size_, other.size_);
    } else if (!is_small() && !other.is_small()) {
      swap(big_, other.big_); // nothrow
      swap(size_, other.size_); // nothrow
    } else {
      SocowVector* small_vector = is_small() ? this : &other;
      SocowVector* big_vector = is_small() ? &other : this;

      Buffer* big_buffer = big_vector->big_;
      std::size_t big_size = big_vector->size_;
      std::size_t small_size = small_vector->size_;
      big_vector->big_ = nullptr;
      std::uninitialized_move_n(small_vector->raw_data(), small_size, big_vector->small_);
      small_vector->destrou_small();
      small_vector->big_ = big_buffer;
      big_vector->size_ = small_size;
      small_vector->size_ = big_size;
      big_vector->is_big = false;
      small_vector->is_big = true;
    }
  }

  std::size_t size() const noexcept {
    return size_;
  }

  std::size_t capacity() const noexcept {
    if (is_small()) {
      return SMALL_SIZE;
    }
    return big_->capacity;
  }

  // nothrow
  bool empty() const noexcept {
    return size_ == 0;
  }

  // O(size) strong garanty, because detauch have a strong garanty
  Reference operator[](std::size_t index) {
    if (is_small()) {
      return small_[index];
    }
    detauch();
    return big_->data_[index];
  }

  // O(1) nothrow, because
  ConstReference operator[](std::size_t index) const {
    if (is_small()) {
      return small_[index];
    }
    return big_->data_[index];
  }

  // O(size) strong garanty
  Pointer data() {
    return begin();
  }

  // O(1) nothrow
  ConstPointer data() const noexcept {
    return begin();
  }

  // O(size) strong garanty
  Reference front() {
    return *begin();
  }

  // O(1) nothrow
  ConstReference front() const {
    return *begin();
  }

  // O(size) strong garanty
  Reference back() {
    return *(end() - 1);
  }

  // O(1) nothrow
  ConstReference back() const {
    return *(end() - 1);
  }

  // O(size) strong
  Iterator begin() {
    detauch();
    return raw_data();
    // if (is_small()) {
    //   return small_;
    // }
    // return big_->data_;
  }

  // O(size) strong
  Iterator end() {
    detauch();
    return raw_data() + size();
    // if (is_small()) {
    //   return small_ + size_;
    // }
    // return big_->data_ + size_;
  }

  // O(1) nothrow
  ConstIterator begin() const noexcept {
    return raw_data();
    // if (is_small()) {
    //   return small_;
    // }
    // return big_->data_;
  }

  // O(1) nothrow
  ConstIterator end() const noexcept {
    return raw_data() + size();
    // if (is_small()) {
    //   return small_ + size_;
    // }
    // return big_->data_ + size_;
  }

  // if small or big_unshared and realocation -> constract new element in new place -> move old elements
  // if big_shared and realocation -> copy old elements -> constract new element in new place
  template <typename U>
  void push_back_me(U&& value) {
    if (is_small()) {
      if (size() < SMALL_SIZE) { // места в маленьком векторе хватает
        new (small_ + size_) T(std::forward<U>(value));
        ++size_;
        return;
      }
      // места в маленьком векторе не хватает -> нужно перейти в большой
      SocowVector tmp(new_capacity(size())); // -> big unshared buffer
      new (tmp.big_->data_ + size()) T(std::forward<U>(value));
      std::uninitialized_move_n(raw_data(), size(), tmp.raw_data());
      tmp.size_ = size() + 1;
      clear_data(); // for small we destruction oll elements in vector and size_ = 0
      swap(tmp);
    } else { // this is a big vector
      if (capacity() > size()) { // место есть
        if (big_->ref_count == 1) { // unshared big buffer
          new (big_->data_ + size()) T(std::forward<U>(value));
          ++size_;
        } else { // shared big buffer
          SocowVector tmp(*this, capacity());
          new (tmp.big_->data_ + size()) T(std::forward<U>(value)); // constract new elements
          ++tmp.size_;
          clear_data();
          swap(tmp);
        }
      } else { // места не хватает -> realocation
        if (big_->ref_count == 1) { // unshared big buffer
          SocowVector tmp(new_capacity(size()));
          new (tmp.big_->data_ + size()) T(std::forward<U>(value));
          try {
            std::uninitialized_move_n(raw_data(), size(), tmp.raw_data());
            tmp.size_ = size();
          } catch (...) {
            (tmp.big_->data_ + size())->~T();
            throw;
          }
          ++tmp.size_;
          clear_data();
          swap(tmp);
        } else {
          SocowVector tmp(*this, new_capacity(size()));
          new (tmp.big_->data_ + size()) T(std::forward<U>(value)); // constract new elements
          ++tmp.size_;
          clear_data();
          swap(tmp);
        }
      }
    }
  }

  // Strong garanty
  void push_back(const T& value) {
    push_back_me(value);
  }

  // Basic garanty, because value maybe in object
  void push_back(T&& value) {
    push_back_me(std::move(value));
  }

  // strong garanty, because detauch have strong garanry
  // if vector is small_mode -> nothrow
  void pop_back() { // no call small_to_big, because capacity not modified
    if (is_small() || (!is_small() && big_->ref_count == 1)) {
      (raw_data() + size() - 1)->~T(); // nothrow
      --size_;
      return;
    }
    SocowVector tmp(*this, size() - 1);
    clear_data();
    swap(tmp);
  }

  // strong
  void reserve(std::size_t new_capacity) {
    if (capacity() < new_capacity || (!is_small() && big_->ref_count > 1 && new_capacity > size())) {
      SocowVector tmp(*this, new_capacity);
      clear_data();
      swap(tmp);
    }
  }

  void shrink_to_fit() {
    if (size() != capacity() && !is_small()) {
      SocowVector tmp(*this, size_);
      clear_data();
      swap(tmp);
    }
  }

  void clear_data() {
    if (is_small()) {
      for (std::size_t index = size(); index > 0; --index) {
        (small_ + index - 1)->~T();
      }
    } else {
      release_ref();
    }
    size_ = 0;
    is_big = false;
  }

  void clear() {
    if (!is_small() && big_->ref_count == 1) {
      for (std::size_t index = size(); index > 0; --index) {
        (big_->data_ + index - 1)->~T();
      }
      size_ = 0;
      return;
    }
    clear_data();
  }

  Iterator erase(ConstIterator pos) {
    return erase(pos, pos + 1);
  }

  Iterator erase(ConstIterator first, ConstIterator last) {
    ConstPointer base = raw_data();
    Pointer data = raw_data();

    std::size_t offset = first - base;
    std::size_t length = last - first;
    std::size_t old_size = size_;
    std::size_t elements = old_size - offset - length;

    if (length == 0) {
      return data + offset;
    }

    if (!is_small() && big_->ref_count > 1) {
      SocowVector tmp(old_size - length);
      Pointer new_data = tmp.is_small() ? tmp.small_ : tmp.big_->data_;
      std::uninitialized_copy_n(data, offset, new_data);
      tmp.size_ = offset;
      std::uninitialized_copy_n(data + offset + length, old_size - (offset + length), new_data + tmp.size());
      tmp.size_ += old_size - (offset + length);
      clear_data();
      swap(tmp);
      return raw_data() + offset;
    }
    for (std::size_t i = 0; i < elements; ++i) {
      using std::swap;
      swap(data[offset + i], data[offset + length + i]);
    }

    for (std::size_t i = old_size; i > old_size - length; --i) {
      (data + i - 1)->~T();
    }

    size_ -= length;
    return data + offset;
  }

  template <typename U>
  Iterator insert_method(ConstIterator& pos, U&& value) {
    ConstPointer base = raw_data();
    std::size_t offset = pos - base;

    push_back(std::forward<U>(value));

    Iterator mutable_pos = raw_data() + offset;
    for (auto it = raw_data() + size() - 1; it != mutable_pos; --it) {
      std::swap(*it, *(it - 1));
    }
    return raw_data() + offset;
  }

  Iterator insert(ConstIterator pos, const T& value) {
    return insert_method(pos, value);
  }

  // O(N) basic garanty, if swap for T no noexcept
  // если же swap noexcept для T, то это strong garanty
  Iterator insert(ConstIterator pos, T&& value) {
    return insert_method(pos, std::move(value));
  }

private:
  Pointer raw_data() noexcept {
    return is_small() ? small_ : big_->data_;
  }

  ConstPointer raw_data() const noexcept {
    return is_small() ? small_ : big_->data_;
  }

  static std::size_t new_capacity(std::size_t capacity) noexcept {
    return (capacity * 2) + 1;
  }

  explicit SocowVector(std::size_t capacity)
      : size_{0}
      , is_big(false) {
    if (capacity > SMALL_SIZE) {
      big_ = static_cast<Buffer*>(operator new(sizeof(Buffer) + (capacity * sizeof(T)), std::align_val_t(alignof(T))));
      big_->capacity = capacity;
      big_->ref_count = 1;
      is_big = true;
    }
  }

  SocowVector(SocowVector& other, std::size_t capacity)
      : size_(0) {
    if (other.is_small()) { // делаем от маленького вектора
      if (capacity > SMALL_SIZE) { // надо перевести в большой вектор
        big_ =
            static_cast<Buffer*>(operator new(sizeof(Buffer) + (capacity * sizeof(T)), std::align_val_t(alignof(T))));
        big_->capacity = capacity;
        big_->ref_count = 1;
        is_big = true;
        std::uninitialized_move_n(other.raw_data(), other.size(), raw_data());
        size_ = other.size();
        return;
      }
      is_big = false;
      std::uninitialized_move_n(other.raw_data(), std::min(capacity, other.size()), raw_data());
      size_ = std::min(capacity, other.size());
      return;
      // capacity <= SMALL_SIZE -> сырая память в small_ уже есть размера больше чем capacity
    }
    // other - big vector
    if (capacity > SMALL_SIZE) {
      big_ = static_cast<Buffer*>(operator new(sizeof(Buffer) + (capacity * sizeof(T)), std::align_val_t(alignof(T))));
      big_->capacity = capacity;
      big_->ref_count = 1;
      is_big = true;
      if (other.big_->ref_count == 1) {
        std::uninitialized_move_n(other.raw_data(), std::min(other.size(), capacity), raw_data());
        size_ = std::min(capacity, other.size_);
      } else {
        try {
          std::uninitialized_copy_n(other.raw_data(), std::min(other.size(), capacity), raw_data());
          size_ = std::min(other.size(), capacity);
        } catch (...) {
          clear_data();
          throw;
        }
      }
    } else { // capacity <= SMALL_SIZE -> this - small mode vector
      is_big = false;
      if (other.big_->ref_count == 1) {
        std::uninitialized_move_n(other.raw_data(), std::min(other.size(), capacity), raw_data());
        size_ = std::min(capacity, other.size_);
      } else {
        try {
          std::uninitialized_copy_n(other.raw_data(), std::min(other.size(), capacity), raw_data());
          size_ = std::min(other.size(), capacity);
        } catch (...) {
          clear_data();
          throw;
        }
      }
      return;
    }
  }

  bool is_small() const noexcept {
    return size() <= SMALL_SIZE && !is_big;
  }

  void release_ref() noexcept {
    if (is_small() || (big_ == nullptr)) {
      return;
    }
    --big_->ref_count;
    if (big_->ref_count == 0) {
      for (std::size_t i = size_; i > 0; --i) {
        (big_->data_ + i - 1)->~T();
      }
      operator delete(big_, std::align_val_t(alignof(T)));
    }
    big_ = nullptr;
  }

  void ref_plus() {
    if (big_ == nullptr) {
      return;
    }
    ++big_->ref_count;
  }
};
} // namespace ct
