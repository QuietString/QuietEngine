#pragma once
#include <string_view>

// Compile-time type name extractor.
// Returns a short, demangled-ish name (e.g., "Player" instead of "class Player").
namespace qtype
{
    namespace detail
    {
        template <typename T>
        constexpr std::string_view Raw()
        {
            // Example: "const char *__cdecl qtype::detail::Raw<struct Player>(void)"
            constexpr std::string_view sig = __FUNCSIG__;
            constexpr std::string_view pfx = "Raw<";
            constexpr std::string_view sfx = ">(void)";

            const auto beg = sig.find(pfx) + pfx.size();
            const auto end = sig.rfind(sfx);
            return sig.substr(beg, end - beg);
        }

        constexpr bool StartsWith(std::string_view s, std::string_view p)
        {
            return s.size() >= p.size() && s.substr(0, p.size()) == p;
        }

        constexpr void StripFront(std::string_view& s, std::string_view tag)
        {
            if (StartsWith(s, tag))
            {
                s.remove_prefix(tag.size());
            }
        }
    }

    // Short, cleaned type name (drops qualifiers like "class ", "struct ", namespaces).
    template <typename T>
    constexpr std::string_view TypeName()
    {
        std::string_view s = detail::Raw<T>();

        // Drop common specifiers prefixes (may appear chained)
        for (;;)
        {
            const auto before = s;
            detail::StripFront(s, "class ");
            detail::StripFront(s, "struct ");
            detail::StripFront(s, "const ");
            detail::StripFront(s, "volatile ");
            if (s == before) break;
        }

        // Keep only the last scope component (after ::)
        if (const auto pos = s.rfind("::"); pos != std::string_view::npos)
        {
            s.remove_prefix(pos + 2);
        }

        // Remove trailing spaces if any
        while (!s.empty() && (s.back() == ' ')) s.remove_suffix(1);

        return s;
    }
}
