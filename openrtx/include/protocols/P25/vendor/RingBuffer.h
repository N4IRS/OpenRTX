/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Templated single-producer/single-consumer ring buffer, API-compatible
 * with the MMDVM-lineage ring buffer the P25 stack expects.
 */
#ifndef P25_VENDOR_RINGBUFFER_H
#define P25_VENDOR_RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

template <class T>
class CRingBuffer
{
public:
    explicit CRingBuffer(size_t capacity)
        : m_buffer(NULL), m_size(0), m_head(0), m_tail(0)
    {
        /* one extra slot: full when (head + 1) % size == tail */
        m_size = capacity + 1U;
        m_buffer = new T[m_size];
    }

    ~CRingBuffer()
    {
        delete[] m_buffer;
    }

    /* Write one element.  Returns false when full. */
    bool put(T value)
    {
        size_t head = m_head;
        size_t next = (head + 1U) % m_size;

        if (next == m_tail)
            return false;

        m_buffer[head] = value;
        m_head = next;
        return true;
    }

    /* Read one element.  Returns false when empty. */
    bool get(T &value)
    {
        size_t tail = m_tail;

        if (tail == m_head)
            return false;

        value = m_buffer[tail];
        m_tail = (tail + 1U) % m_size;
        return true;
    }

    /* Number of elements currently available. */
    size_t getData() const
    {
        size_t head = m_head;
        size_t tail = m_tail;

        if (head >= tail)
            return head - tail;

        return m_size + head - tail;
    }

    /* Free slots available for writing. */
    size_t getSpace() const
    {
        return (m_size - 1U) - getData();
    }

    /* Discard all contents (safe only when no concurrent producer). */
    void reset()
    {
        m_head = 0;
        m_tail = 0;
    }

private:
    T *m_buffer;
    volatile size_t m_size;
    volatile size_t m_head;
    volatile size_t m_tail;
};

#endif /* P25_VENDOR_RINGBUFFER_H */
