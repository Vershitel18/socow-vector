#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace ct {

template <typename T, std::size_t SMALL_SIZE>
class SocowVector {
  static_assert(std::is_copy_constructible_v<T>, "T must have a copy constructor");
  static_assert(std::is_nothrow_move_constructible_v<T>, "T must have a non-throwing move constructor");
  static_assert(std::is_copy_assignable_v<T>, "T must have a copy assignment operator");
  static_assert(std::is_nothrow_move_assignable_v<T>, "T must have a non-throwing move assignment operator");
  static_assert(std::is_nothrow_swappable_v<T>, "T must have a non-throwing swap");
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
  SocowVector() noexcept
      : size_(0)
      , is_big(false) {}

  // strong guarantee
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

  // nothrow, if T move constructor is nothrow
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

  // strong guarantee
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
    clear_data();
  }

  // nothrow, if T swap and T move constructor are nothrow
  void swap(SocowVector& other) noexcept {
    if (this == &other) {
      return;
    }
    using std::swap;
    if (is_small() && other.is_small()) {
      std::size_t index = 0;
      for (; index < std::min(size_, other.size_); ++index) {
        swap(small_[index], other.small_[index]);
      }
      SocowVector* small_vector = size_ < other.size_ ? this : &other;
      SocowVector* big_vector = size_ < other.size_ ? &other : this;
      std::uninitialized_move_n(
          big_vector->raw_data() + index,
          big_vector->size() - index,
          small_vector->raw_data() + index
      );
      for (; index < big_vector->size_; ++index) {
        big_vector->small_[index].~T();
      }
      swap(size_, other.size_);
      return;
    }
    if (!is_small() && !other.is_small()) {
      swap(big_, other.big_);
      swap(size_, other.size_);
      return;
    }
    SocowVector* small_vector = is_small() ? this : &other;
    SocowVector* big_vector = is_small() ? &other : this;
    Buffer* big_buffer = big_vector->big_;
    std::size_t big_size = big_vector->size_;
    std::size_t small_size = small_vector->size_;
    big_vector->big_ = nullptr;
    std::uninitialized_move_n(small_vector->raw_data(), small_size, big_vector->small_);
    small_vector->destroy_small();
    small_vector->big_ = big_buffer;
    big_vector->size_ = small_size;
    small_vector->size_ = big_size;
    big_vector->is_big = false;
    small_vector->is_big = true;
  }

  // nothrow
  std::size_t size() const noexcept {
    return size_;
  }

  // nothrow
  std::size_t capacity() const noexcept {
    return is_small() ? SMALL_SIZE : big_->capacity;
  }

  // nothrow
  bool empty() const noexcept {
    return size_ == 0;
  }

  // strong guarantee
  Reference operator[](std::size_t index) {
    if (is_small()) {
      return small_[index];
    }
    detach();
    return big_->data_[index];
  }

  // nothrow
  ConstReference operator[](std::size_t index) const noexcept {
    return is_small() ? small_[index] : big_->data_[index];
  }

  // strong guarantee
  Pointer data() {
    return begin();
  }

  // nothrow
  ConstPointer data() const noexcept {
    return begin();
  }

  // strong guarantee
  Reference front() {
    return *begin();
  }

  // nothrow
  ConstReference front() const noexcept {
    return *begin();
  }

  // strong guarantee
  Reference back() {
    return *(end() - 1);
  }

  // nothrow
  ConstReference back() const noexcept {
    return *(end() - 1);
  }

  // strong guarantee
  Iterator begin() {
    detach();
    return raw_data();
  }

  // strong guarantee
  Iterator end() {
    detach();
    return raw_data() + size();
  }

  // nothrow
  ConstIterator begin() const noexcept {
    return raw_data();
  }

  // nothrow
  ConstIterator end() const noexcept {
    return raw_data() + size();
  }

  // strong guarantee
  void push_back(const T& value) {
    push_back_me(value);
  }

  // basic guarantee
  void push_back(T&& value) {
    push_back_me(std::move(value));
  }

  // strong guarantee
  void pop_back() {
    if (is_small() || unshared()) {
      (raw_data() + size() - 1)->~T();
      --size_;
      return;
    }

    swap_tmp_size(size() - 1);
  }

  // strong guarantee
  void reserve(std::size_t new_capacity) {
    if (capacity() < new_capacity || (is_shared() && new_capacity > size())) {
      swap_tmp_size(new_capacity);
    }
  }

  // strong guarantee
  void shrink_to_fit() {
    if (size() != capacity() && !is_small()) {
      swap_tmp_size(size());
    }
  }

  // nothrow
  void clear() {
    if (unshared()) {
      for (std::size_t index = size(); index > 0; --index) {
        (raw_data() + index - 1)->~T();
      }
      size_ = 0;
      return;
    }
    clear_data();
  }

  // basic guarantee
  Iterator erase(ConstIterator pos) {
    return erase(pos, pos + 1);
  }

  // basic guarantee
  Iterator erase(ConstIterator first, ConstIterator last) {
    ConstPointer base = raw_data();
    Pointer data = raw_data();

    std::size_t offset = first - base;
    std::size_t length = last - first;
    const std::size_t old_size = size_;
    const std::size_t elements = old_size - offset - length;

    if (length == 0) {
      return data + offset;
    }

    if (is_shared()) {
      SocowVector tmp(old_size - length);
      Pointer new_data = tmp.raw_data();

      std::uninitialized_copy_n(data, offset, new_data);
      tmp.size_ = offset;

      std::uninitialized_copy_n(data + offset + length, old_size - offset - length, new_data + tmp.size());
      tmp.size_ += old_size - offset - length;

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

  // strong
  Iterator insert(ConstIterator pos, const T& value) {
    return insert_method(pos, value);
  }

  // basic guarantee
  Iterator insert(ConstIterator pos, T&& value) {
    return insert_method(pos, std::move(value));
  }

private:
  static Buffer* allocate(const std::size_t capacity) {
    Buffer* big =
        static_cast<Buffer*>(operator new(sizeof(Buffer) + capacity * sizeof(T), std::align_val_t(alignof(T))));

    big->capacity = capacity;
    big->ref_count = 1;

    return big;
  }

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
      big_ = allocate(capacity);
      is_big = true;
    }
  }

  SocowVector(SocowVector& other, std::size_t capacity)
      : size_(0) {
    if (other.is_small()) {
      if (capacity > SMALL_SIZE) {
        big_ = allocate(capacity);
        is_big = true;
        std::uninitialized_move_n(other.raw_data(), other.size(), raw_data());
        size_ = other.size();
        return;
      }
      is_big = false;
      std::uninitialized_move_n(other.raw_data(), std::min(capacity, other.size()), raw_data());
      size_ = std::min(capacity, other.size());
      return;
    }
    if (capacity > SMALL_SIZE) {
      big_ = allocate(capacity);
      is_big = true;
      if (other.unshared()) {
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
    is_big = false;
    if (other.unshared()) {
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
  }

  // nothrow
  bool is_small() const noexcept {
    return size() <= SMALL_SIZE && !is_big;
  }

  // strong guarantee
  void detach() {
    if (is_shared()) {
      Buffer* buffer = allocate(big_->capacity);
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

  // nothrow
  void destroy_small() noexcept {
    for (std::size_t i = 0; i < size(); ++i) {
      small_[i].~T();
    }
  }

  // nothrow
  void release_ref() noexcept {
    if (!is_small()) {
      --big_->ref_count;
      if (big_->ref_count == 0) {
        for (std::size_t i = size_; i > 0; --i) {
          (big_->data_ + i - 1)->~T();
        }
        operator delete(big_, std::align_val_t(alignof(T)));
      }
      big_ = nullptr;
    }
  }

  // nothrow
  void ref_plus() noexcept {
    if (big_ == nullptr) {
      return;
    }
    ++big_->ref_count;
  }

  // nothrow
  void clear_data() noexcept {
    if (is_small()) {
      destroy_small();
    } else {
      release_ref();
    }
    size_ = 0;
    is_big = false;
  }

  // strong guarantee
  void swap_tmp_size(std::size_t size) {
    SocowVector tmp(*this, size);
    clear_data();
    swap(tmp);
  }

  bool is_shared() const noexcept {
    return !is_small() && big_->ref_count > 1;
  }

  bool unshared() const noexcept {
    return !is_small() && big_->ref_count == 1;
  }

  template <typename U>
  void push_to_tmp(SocowVector& tmp, U&& value) {
    new (tmp.raw_data() + size()) T(std::forward<U>(value));
    ++tmp.size_;
    clear_data();
    swap(tmp);
  }

  template <typename U>
  void push_back_me(U&& value) {
    if (capacity() > size()) {
      if (is_shared()) {
        SocowVector tmp(*this, capacity());
        push_to_tmp(tmp, std::forward<U>(value));
        return;
      }
      new (raw_data() + size_) T(std::forward<U>(value));
      ++size_;
      return;
    }
    if (is_small()) {
      SocowVector tmp(new_capacity(size()));
      new (tmp.raw_data() + size()) T(std::forward<U>(value));
      std::uninitialized_move_n(raw_data(), size(), tmp.raw_data());
      tmp.size_ = size() + 1;
      clear_data();
      swap(tmp);
      return;
    }
    if (unshared()) {
      SocowVector tmp(new_capacity(size()));
      new (tmp.raw_data() + size()) T(std::forward<U>(value));
      try {
        std::uninitialized_move_n(raw_data(), size(), tmp.raw_data());
        tmp.size_ = size() + 1;
      } catch (...) {
        (tmp.raw_data() + size())->~T();
        throw;
      }
      clear_data();
      swap(tmp);
      return;
    }
    SocowVector tmp(*this, new_capacity(size()));
    push_to_tmp(tmp, std::forward<U>(value));
  }

  template <typename U>
  Iterator insert_method(ConstIterator pos, U&& value) {
    ConstPointer base = raw_data();
    std::size_t offset = pos - base;
    push_back(std::forward<U>(value));
    Iterator mutable_pos = raw_data() + offset;
    for (auto it = raw_data() + size() - 1; it != mutable_pos; --it) {
      std::swap(*it, *(it - 1));
    }
    return raw_data() + offset;
  }
};

} // namespace ct
