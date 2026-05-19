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

  // strong garanty
  SocowVector(const SocowVector& other) {
    if (!is_small(other)) {
      big_ = other.big_;
      ref_plus();
      is_big = true;
    } else {
      std::size_t index = 0;
      try {
        for (; index < other.size_; ++index) {
          new (small_ + index) T(other.small_[index]);
        }
      } catch (...) {
        for (; index > 0; --index) {
          small_[index - 1].~T();
        }
        throw;
      }
      is_big = false;
    }
    size_ = other.size_;
  }

  // nothrow
  SocowVector(SocowVector&& other) noexcept {
    if (!is_small(other)) {
      big_ = other.big_;
      other.big_ = nullptr;
      size_ = other.size_;
      other.size_ = 0;
      other.is_big = false;
      is_big = true;
    } else {
      for (std::size_t index = 0; index < other.size_; ++index) {
        new (small_ + index) T(std::move(other.small_[index]));
      }
      size_ = other.size_;
      is_big = false;
    }
  }

  // strong garanty
  SocowVector& operator=(const SocowVector& other) {
    if (this != &other) {
      if (is_small(other)) {
        SocowVector tmp{};
        for (std::size_t index = 0; index < other.size_; ++index) {
          new (tmp.small_ + index) T(other.small_[index]);
          ++tmp.size_;
        }
        if (is_small(*this)) {
          destroy_small();
        } else {
          release_ref();
          big_ = nullptr;
          is_big = false;
        }
        for (std::size_t i = 0; i < tmp.size(); ++i) {
          new (small_ + i) T(std::move(tmp.small_[i]));
        }
        size_ = tmp.size();
        is_big = false;
      } else {
        if (is_small(*this)) {
          destroy_small();
        }
        release_ref();
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
      if (is_small(other)) {
        if (is_small(*this)) {
          destroy_small();
        } else {
          release_ref();
          big_ = nullptr;
          is_big = false;
        }
        for (std::size_t i = 0; i < other.size(); ++i) {
          new (small_ + i) T(std::move(other.small_[i]));
        }
        size_ = other.size();
        is_big = false;
      } else {
        if (is_small(*this)) {
          destroy_small();
        } else {
          release_ref();
          big_ = nullptr;
        }
        big_ = other.big_;
        size_ = other.size();
        is_big = true;
        other.big_ = nullptr;
        other.size_ = 0;
        other.is_big = false;
      }
    }
    return *this;
  }

  // notrhrow
  ~SocowVector() {
    if (!is_small(*this)) {
      release_ref();
      big_ = nullptr;
    } else {
      for (std::size_t i = size_; i > 0; --i) {
        small_[i - 1].~T();
      }
    }
    size_ = 0;
  }

  void destroy_small() {
    for (std::size_t i = size_; i > 0; --i) {
      small_[i - 1].~T();
    }
  }

  void small_to_big(SocowVector& object, std::size_t new_capacity) {
    Buffer* buffer =
        static_cast<Buffer*>(operator new(sizeof(Buffer) + (new_capacity * sizeof(T)), std::align_val_t(alignof(T))));
    buffer->capacity = new_capacity;
    buffer->ref_count = 1;

    std::size_t index = 0;
    try {
      for (; index < object.size_; ++index) {
        new (buffer->data_ + index) T(object.small_[index]);
      }
    } catch (...) {
      for (; index > 0; --index) {
        (buffer->data_ + index - 1)->~T();
      }
      operator delete(buffer, std::align_val_t(alignof(T)));
      throw;
    }

    for (std::size_t i = object.size_; i > 0; --i) {
      object.small_[i - 1].~T();
    }
    object.big_ = buffer;
    object.is_big = true;
  }

  void big_to_small(SocowVector& object) {
    if (object.size() <= SMALL_SIZE && object.is_big) {
      SocowVector tmp(object, SMALL_SIZE);
      object.swap(tmp);
    }
  }

  // strong garanty
  void detauch() {
    if (!is_small(*this) && this->big_->ref_count > 1) {
      Buffer* buffer = static_cast<Buffer*>(operator new(
          sizeof(Buffer) + (big_->capacity * sizeof(T)),
          std::align_val_t(alignof(T))
      ));
      buffer->capacity = big_->capacity;
      buffer->ref_count = 1;
      std::size_t index = 0;
      try {
        for (; index < size_; ++index) {
          new (buffer->data_ + index) T(big_->data_[index]);
        }
      } catch (...) {
        for (; index > 0; --index) {
          (buffer->data_ + index - 1)->~T();
        }
        operator delete(buffer, std::align_val_t(alignof(T)));
        throw;
      }
      release_ref();
      big_ = buffer;
    }
  }

  // nothrow
  void swap(SocowVector& other) noexcept {
    if (this == &other) {
      return;
    }
    using std::swap;
    if (is_small(*this) && is_small(other)) {
      std::size_t index = 0;
      for (; index < std::min(this->size_, other.size_); ++index) {
        swap(small_[index], other.small_[index]); // nothrow
      }
      SocowVector* small_vector = this->size_ < other.size_ ? this : &other;
      SocowVector* big_vector = this->size_ < other.size_ ? &other : this;
      for (; index < big_vector->size_; ++index) {
        new (small_vector->small_ + index) T(std::move(big_vector->small_[index])); // nothrow
        big_vector->small_[index].~T();
      }
      swap(size_, other.size_);
    } else if (!is_small(*this) && !is_small(other)) {
      swap(big_, other.big_); // nothrow
      swap(size_, other.size_); // nothrow
    } else {
      SocowVector* small_vector = is_small(*this) ? this : &other;
      SocowVector* big_vector = is_small(*this) ? &other : this;

      Buffer* big_buffer = big_vector->big_;
      std::size_t big_size = big_vector->size_;
      std::size_t small_size = small_vector->size_;
      big_vector->big_ = nullptr;
      for (std::size_t i = 0; i < small_size; ++i) {
        new (big_vector->small_ + i) T(std::move(small_vector->small_[i])); // nothrow
        small_vector->small_[i].~T(); // nothrow
      }
      small_vector->big_ = big_buffer;
      big_vector->size_ = small_size;
      small_vector->size_ = big_size;
      big_vector->is_big = false;
      small_vector->is_big = true;
    }
  }

  std::size_t size() const {
    return size_;
  }

  std::size_t capacity() const {
    if (is_small(*this)) {
      return SMALL_SIZE;
    }
    return big_->capacity;
  }

  bool empty() const {
    return size_ == 0;
  }

  //
  // (size) strong garanty
  Reference operator[](std::size_t index) {
    if (is_small(*this)) {
      return small_[index];
    }
    detauch();
    return big_->data_[index];
  }

  // O(1) nothrow
  ConstReference operator[](std::size_t index) const {
    if (is_small(*this)) {
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
    if (is_small(*this)) {
      return small_;
    }
    return big_->data_;
  }

  // O(size) strong
  Iterator end() {
    detauch();
    if (is_small(*this)) {
      return small_ + size_;
    }
    return big_->data_ + size_;
  }

  // O(1) nothrow
  ConstIterator begin() const noexcept {
    if (is_small(*this)) {
      return small_;
    }
    return big_->data_;
  }

  // O(1) nothrow
  ConstIterator end() const noexcept {
    if (is_small(*this)) {
      return small_ + size_;
    }
    return big_->data_ + size_;
  }

  // if small or big_unshared and realocation -> constract new element in new place -> move old elements
  // if big_shared and realocation -> copy old elements -> constract new element in new place
  template <typename U>
  void push_back_me(U&& value) {
    if (is_small(*this)) {
      if (size() < SMALL_SIZE) { // места в маленьком векторе хватает
        new (small_ + size_) T(std::forward<U>(value));
        ++size_;
        return;
      }
      // места в маленьком векторе не хватает -> нужно перейти в большой
      SocowVector tmp(new_capacity(size())); // -> big unshared buffer
      new (tmp.big_->data_ + size()) T(std::forward<U>(value));
      std::uninitialized_move_n(raw_data(), size(), tmp.raw_data());
      // for (std::size_t index = 0; index < size(); ++index) {
      //   new (tmp.big_->data_ + index) T(std::move(small_[index])); // move old elements
      //   ++tmp.size_;
      // }
      tmp.size_ = size() + 1;
      clear(); // for small we destruction oll elements in vector and size_ = 0
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
          clear();
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
          // for (std::size_t index = 0; index < size(); ++index ) {
          //   new (tmp.big_->data_ + index) T(std::move(big_->data_[index]));
          //   ++tmp.size_;
          // }
          ++tmp.size_;
          clear();
          swap(tmp);
        } else {
          SocowVector tmp(new_capacity(size()));
          new (tmp.big_->data_ + size()) T(std::forward<U>(value));
          try {
            for (std::size_t index = 0; index < size(); ++index) {
              new (tmp.big_->data_ + index) T(big_->data_[index]);
              ++tmp.size_;
            }
          } catch (...) {
            (tmp.big_->data_ + size())->~T();
            throw;
          }
          ++tmp.size_;
          clear();
          swap(tmp);
        }
      }
    }
  }

  // template <typename U>
  // void push_back_method(U&& value) {
  //   if (is_small(*this)) {
  //     if (size_ < SMALL_SIZE) {
  //       new (small_ + size_) T(std::forward<U>(value));
  //       ++size_;
  //       return;
  //     }
  //
  //     SocowVector tmp(*this, new_capacity(size_));
  //
  //     new (tmp.big_->data_ + tmp.size_) T(std::forward<U>(value));
  //     ++tmp.size_;
  //
  //     clear_data(*this);
  //
  //     big_ = tmp.big_;
  //     tmp.big_ = nullptr;
  //     is_big = true;
  //     size_ = tmp.size_;
  //     return;
  //   }
  //
  //   if (big_->capacity > size_) {
  //     if (big_->ref_count == 1) {
  //       new (big_->data_ + size_) T(std::forward<U>(value));
  //       ++size_;
  //       return;
  //     }
  //
  //     SocowVector tmp(*this, big_->capacity);
  //
  //     new (tmp.big_->data_ + tmp.size_) T(std::forward<U>(value));
  //     ++tmp.size_;
  //
  //     clear_data(*this);
  //
  //     big_ = tmp.big_;
  //     tmp.big_ = nullptr;
  //     is_big = true;
  //     size_ = tmp.size_;
  //     return;
  //   }
  //
  //   SocowVector tmp(new_capacity(big_->capacity));
  //
  //   if (big_->ref_count > 1) {
  //     for (std::size_t i = 0; i < size_; ++i) {
  //       new (tmp.big_->data_ + tmp.size_) T(big_->data_[i]);
  //       ++tmp.size_;
  //     }
  //
  //     new (tmp.big_->data_ + tmp.size_) T(std::forward<U>(value));
  //     ++tmp.size_;
  //   } else {
  //     new (tmp.big_->data_ + size_) T(std::forward<U>(value));
  //
  //     try {
  //       for (std::size_t i = 0; i < size_; ++i) {
  //         new (tmp.big_->data_ + tmp.size_) T(std::move(big_->data_[i]));
  //         ++tmp.size_;
  //       }
  //     } catch (...) {
  //       (tmp.big_->data_ + size_)->~T();
  //       throw;
  //     }
  //
  //     ++tmp.size_;
  //   }
  //
  //   clear_data(*this);
  //
  //   big_ = tmp.big_;
  //   tmp.big_ = nullptr;
  //   is_big = true;
  //   size_ = tmp.size_;
  // }

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
    if (size() != 0) {
      if (is_small(*this)) {
        (small_ + size() - 1)->~T(); // nothrow
        --size_;
      } else {
        if (big_->ref_count == 1) {
          (big_->data_ + size() - 1)->~T(); // nothrow
          --size_;
          return;
        }
        // detauch(); // strong garanty
        // (big_->data_ + size() - 1)->~T(); // nothrow
        // --size_;
        SocowVector tmp(*this, size() - 1);
        clear();
        swap(tmp);
      }
    }
  }

  // strong
  void reserve(std::size_t new_capacity) {
    if (is_small(*this) && new_capacity <= SMALL_SIZE) {
      return;
    }
    if (capacity() < new_capacity || (!is_small(*this) && big_->ref_count > 1 && new_capacity > size_)) {
      SocowVector tmp(*this, new_capacity);
      clear();
      swap(tmp);
      return;
    }
  }

  void shrink_to_fit() {
    if (size() == capacity()) {
      return;
    }
    if (is_small(*this)) {
      return;
    }
    SocowVector tmp(*this, size_);
    clear();
    swap(tmp);
  }

  void clear_data(SocowVector& object) {
    if (is_small(object)) {
      for (std::size_t index = object.size(); index > 0; --index) {
        (object.small_ + index - 1)->~T();
      }
    } else {
      object.release_ref();
    }
  }

  void clear() {
    if (is_small(*this)) {
      for (std::size_t index = size(); index > 0; --index) {
        (small_ + index - 1)->~T();
      }
      size_ = 0;
    } else {
      if (big_->ref_count == 1) {
        for (std::size_t index = size(); index > 0; --index) {
          (big_->data_ + index - 1)->~T();
        }
        size_ = 0;
        return;
      }
      release_ref();
      size_ = 0;
      is_big = false;
    }
  }

  Pointer raw_data() noexcept {
    return is_small(*this) ? small_ : big_->data_;
  }

  ConstPointer raw_data() const noexcept {
    return is_small(*this) ? small_ : big_->data_;
  }

  Iterator erase(ConstIterator pos) {
    return erase(pos, pos + 1);
  }

  Iterator erase(ConstIterator first, ConstIterator last) {
    ConstPointer base = static_cast<const SocowVector&>(*this).begin();
    Pointer data = is_small(*this) ? small_ : big_->data_;

    std::size_t offset = first - base;
    std::size_t length = last - first;
    std::size_t old_size = size_;
    std::size_t elements = old_size - offset - length;

    if (length == 0) {
      return data + offset;
    }

    if (!is_small(*this) && big_->ref_count > 1) {
      SocowVector tmp(old_size - length);
      Pointer new_data = is_small(tmp) ? tmp.small_ : tmp.big_->data_;

      for (std::size_t i = 0; i < offset; ++i) {
        new (new_data + tmp.size_) T(data[i]);
        ++tmp.size_;
      }

      for (std::size_t i = offset + length; i < old_size; ++i) {
        new (new_data + tmp.size_) T(data[i]);
        ++tmp.size_;
      }
      clear();
      swap(tmp);
      return (is_small(*this) ? small_ : big_->data_) + offset;
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

  Iterator insert(ConstIterator pos, const T& value) {
    ConstPointer base = static_cast<const SocowVector&>(*this).begin();
    std::size_t offset = pos - base;


    push_back(value);

    Iterator mutable_pos = raw_data() + offset;
    for (auto it = raw_data() + size() - 1; it != mutable_pos; --it) {
      std::swap(*it, *(it - 1));
    }

    return raw_data() + offset;
  }

  // O(N) basic garanty, if swap for T no noexcept
  // если же swap noexcept для T, то это strong garanty
  Iterator insert(ConstIterator pos, T&& value) {
    ConstPointer base = static_cast<const SocowVector&>(*this).begin();
    std::size_t offset = pos - base;

    push_back(std::move(value));

    Iterator mutable_pos = raw_data() + offset;
    for (auto it = raw_data() + size() - 1; it != mutable_pos; --it) {
      std::swap(*it, *(it - 1));
    }

    return raw_data() + offset;
  }

private:
  static std::size_t new_capacity(std::size_t capacity) noexcept {
    return capacity * 2 + 1;
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
    if (is_small(other)) { // делаем от маленького вектора
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
      // for (std::size_t index = 0; index < std::min(capacity, other.size()); ++index) {
      //   new (small_ + index) T(std::move(other.small_[index]));
      //   ++size_;
      // }
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
        // for (std::size_t index = 0; index < std::min(other.size(), capacity); ++index) {
        //   new (big_->data_ + index) T(std::move(other.big_->data_[index]));
        //   ++size_;
        // }
        size_ = std::min(capacity, other.size_);
      } else {
        try {
          for (std::size_t index = 0; index < std::min(other.size(), capacity); ++index) {
            new (big_->data_ + index) T(other.big_->data_[index]);
            ++size_;
          }
        } catch (...) {
          clear();
          throw;
        }
      }
    } else { // capacity <= SMALL_SIZE -> this - small mode vector
      is_big = false;
      if (other.big_->ref_count == 1) {
        std::uninitialized_move_n(other.raw_data(), std::min(other.size(), capacity), raw_data());
        // for (std::size_t index = 0; index < std::min(other.size(), capacity); ++index) {
        //   new (big_->data_ + index) T(std::move(other.big_->data_[index]));
        //   ++size_;
        // }
        size_ = std::min(capacity, other.size_);
      } else {
        try {
          for (std::size_t index = 0; index < std::min(other.size_, capacity); ++index) {
            new (small_ + index) T(other.big_->data_[index]);
            ++size_;
          }
        } catch (...) {
          clear();
          throw;
        }
      }
      return;
    }
  }

  static bool is_small(const SocowVector& object) {
    return object.size_ <= SMALL_SIZE && object.is_big == false;
  }

  void release_ref() noexcept {
    if (is_small(*this) || (big_ == nullptr)) {
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
