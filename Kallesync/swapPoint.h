#pragma once

template <typename T>
void swapPtrs(T*& a, T*& b) {
    T* tmp = a;
    a = b;
    b = tmp;
}