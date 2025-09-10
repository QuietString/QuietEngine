#pragma once
#include <streambuf>
#include <atomic>
#include <ostream>

class DirtyStreamBuffer : public std::streambuf
{
public:
    explicit DirtyStreamBuffer(std::streambuf* dest) : dest_(dest) {}

    bool has_pending() const noexcept
    {
        return dirty_.load(std::memory_order_relaxed);
    }

protected:
    // Called when one character is put
    int_type overflow(int_type ch) override
    {
        if (!traits_type::eq_int_type(ch, traits_type::eof()))
            {
            dirty_.store(true, std::memory_order_relaxed);
            return dest_->sputc(traits_type::to_char_type(ch));
        }
        // Do nothing on EOF here; real flushing happens in sync().
        return traits_type::not_eof(ch);
    }

    // Called when multiple characters are put
    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        if (n > 0) dirty_.store(true, std::memory_order_relaxed);
        return dest_->sputn(s, n);
    }

    // Flush underlying buffer
    int sync() override
    {
        int r = dest_->pubsync();
        if (r == 0) dirty_.store(false, std::memory_order_relaxed);
        return r;
    }

private:
    std::streambuf* dest_;
    std::atomic<bool> dirty_{false};
};
