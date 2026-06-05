#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dir.h>

namespace emu::filesystem {
    enum class file_type {
        none,
        not_found,
        regular,
        directory,
    };

    class file_status {
    public:
        file_status() = default;
        explicit file_status(file_type type) : type_(type) {}

        file_type type() const
        {
            return type_;
        }

    private:
        file_type type_ = file_type::none;
    };

    enum class directory_options : unsigned {
        none = 0,
        skip_permission_denied = 1u,
    };

    namespace detail {
        inline bool is_separator(char ch)
        {
            return ch == '/' || ch == '\\';
        }

        inline bool has_drive_prefix(const std::string &value)
        {
#ifdef _WIN32
            return value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':';
#else
            (void) value;
            return false;
#endif
        }

        inline std::string normalize_separators(std::string value)
        {
            if (value.empty())
                return value;

            std::vector<char> buffer(value.begin(), value.end());
            buffer.push_back('\0');
            path_normalize(buffer.data());
            return std::string(buffer.data());
        }

        inline size_t root_length(const std::string &value)
        {
            if (value.empty())
                return 0;

#ifdef _WIN32
            if (has_drive_prefix(value)) {
                if (value.size() >= 3 && is_separator(value[2]))
                    return 3;
                return 2;
            }
#endif

            return is_separator(value[0]) ? 1u : 0u;
        }

        inline bool is_root_path(const std::string &value)
        {
            const size_t root = root_length(value);
            return root != 0 && value.size() == root;
        }

        inline std::string strip_trailing_separators(std::string value)
        {
            const size_t root = root_length(value);
            while (value.size() > root && !value.empty() && is_separator(value.back()))
                value.pop_back();
            return value;
        }

        inline std::string normalize_lexically(std::string value)
        {
            value = normalize_separators(std::move(value));
            if (value.empty())
                return value;

            std::string root;
            bool absolute = false;
            size_t index = 0;

#ifdef _WIN32
            if (has_drive_prefix(value)) {
                root = value.substr(0, 2);
                index = 2;
                if (value.size() > index && is_separator(value[index])) {
                    absolute = true;
                    index++;
                }
            } else if (is_separator(value[0])) {
                absolute = true;
                index = 1;
            }
#else
            if (is_separator(value[0])) {
                absolute = true;
                index = 1;
            }
#endif

            std::vector<std::string> segments;
            while (index <= value.size()) {
                const size_t next = value.find_first_of("/\\", index);
                const size_t end = (next == std::string::npos) ? value.size() : next;
                const std::string segment = value.substr(index, end - index);

                if (!segment.empty() && segment != ".") {
                    if (segment == "..") {
                        if (!segments.empty() && segments.back() != "..")
                            segments.pop_back();
                        else if (!absolute)
                            segments.push_back(segment);
                    } else {
                        segments.push_back(segment);
                    }
                }

                if (next == std::string::npos)
                    break;
                index = next + 1;
            }

            std::string result = root;
            if (absolute)
                result.push_back('/');

            for (size_t segment_index = 0; segment_index < segments.size(); segment_index++) {
                if (!result.empty() && !is_separator(result.back()))
                    result.push_back('/');
                result += segments[segment_index];
            }

            if (result.empty())
                return absolute ? std::string("/") : root;
            return result;
        }

        inline bool is_absolute_path(const std::string &value)
        {
            if (value.empty())
                return false;

            std::vector<char> buffer(value.begin(), value.end());
            buffer.push_back('\0');
            return path_abs(buffer.data()) != 0;
        }

        inline std::string append_path(const std::string &lhs, const std::string &rhs)
        {
            if (lhs.empty())
                return normalize_separators(rhs);
            if (rhs.empty())
                return normalize_separators(lhs);

            std::vector<char> buffer(lhs.size() + rhs.size() + 4, '\0');
            path_append_filename(buffer.data(), lhs.c_str(), rhs.c_str());
            return normalize_separators(std::string(buffer.data()));
        }

        inline file_status classify_directory_entry(plat_dir_t *context)
        {
            if (plat_dir_is_dir(context))
                return file_status(file_type::directory);
            if (plat_dir_is_file(context))
                return file_status(file_type::regular);
            return file_status(file_type::none);
        }
    };

    class path {
    public:
        using string_type = std::string;

#ifdef _WIN32
        static constexpr char preferred_separator = '\\';
#else
        static constexpr char preferred_separator = '/';
#endif

        path() = default;
        path(const char *value) : native_(value != nullptr ? detail::normalize_separators(value) : std::string()) {}
        path(std::string value) : native_(detail::normalize_separators(std::move(value))) {}

        bool empty() const
        {
            return native_.empty();
        }

        void clear()
        {
            native_.clear();
        }

        std::string string() const
        {
            return native_;
        }

        std::string generic_string() const
        {
            return native_;
        }

        const std::string &native() const
        {
            return native_;
        }

        path lexically_normal() const
        {
            return path(detail::normalize_lexically(native_));
        }

        path parent_path() const
        {
            const std::string value = detail::strip_trailing_separators(native_);
            if (value.empty())
                return {};
            if (detail::is_root_path(value))
                return path(value);

            const size_t separator = value.find_last_of("/\\");
            if (separator == std::string::npos) {
#ifdef _WIN32
                if (detail::has_drive_prefix(value))
                    return path(value.substr(0, 2));
#endif
                return {};
            }

#ifdef _WIN32
            if (separator == 2 && detail::has_drive_prefix(value))
                return path(value.substr(0, 3));
#endif
            if (separator == 0)
                return path(value.substr(0, 1));
            return path(value.substr(0, separator));
        }

        path filename() const
        {
            const std::string value = detail::strip_trailing_separators(native_);
            if (value.empty() || detail::is_root_path(value))
                return {};

            const size_t separator = value.find_last_of("/\\");
            if (separator == std::string::npos) {
#ifdef _WIN32
                if (detail::has_drive_prefix(value) && value.size() > 2)
                    return path(value.substr(2));
#endif
                return path(value);
            }

            return path(value.substr(separator + 1));
        }

        bool is_absolute() const
        {
            return detail::is_absolute_path(native_);
        }

        friend path operator/(const path &lhs, const path &rhs)
        {
            if (rhs.empty())
                return lhs;
            if (rhs.is_absolute())
                return rhs;
            if (lhs.empty())
                return rhs;
            return path(detail::append_path(lhs.native_, rhs.native_));
        }

        friend bool operator==(const path &lhs, const path &rhs)
        {
            return lhs.native_ == rhs.native_;
        }

        friend bool operator!=(const path &lhs, const path &rhs)
        {
            return !(lhs == rhs);
        }

    private:
        std::string native_;
    };

    class directory_entry {
    public:
        directory_entry() = default;
        directory_entry(path entry_path, file_status status) : path_(std::move(entry_path)), status_(status) {}

        const path &path() const
        {
            return path_;
        }

        file_status status(std::error_code &error) const
        {
            error.clear();
            return status_;
        }

    private:
        class path        path_;
        class file_status status_;
    };

    class directory_iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = directory_entry;
        using difference_type = std::ptrdiff_t;
        using pointer = const directory_entry *;
        using reference = const directory_entry &;

        directory_iterator() = default;

        directory_iterator(const path &directory, directory_options, std::error_code &error)
        {
            auto state = std::make_shared<state_type>();
            if (!state->open(directory.generic_string().c_str())) {
                error = std::make_error_code(std::errc::no_such_file_or_directory);
                return;
            }

            error.clear();
            state_ = std::move(state);
            advance();
        }

        reference operator*() const
        {
            return state_->current;
        }

        pointer operator->() const
        {
            return &state_->current;
        }

        directory_iterator &operator++()
        {
            advance();
            return *this;
        }

        directory_iterator begin() const
        {
            return *this;
        }

        directory_iterator end() const
        {
            return {};
        }

        friend bool operator==(const directory_iterator &lhs, const directory_iterator &rhs)
        {
            const bool lhs_end = !lhs.state_ || lhs.state_->at_end;
            const bool rhs_end = !rhs.state_ || rhs.state_->at_end;
            if (lhs_end || rhs_end)
                return lhs_end == rhs_end;
            return lhs.state_ == rhs.state_;
        }

        friend bool operator!=(const directory_iterator &lhs, const directory_iterator &rhs)
        {
            return !(lhs == rhs);
        }

    private:
        struct state_type {
            state_type()
            {
#ifdef _WIN32
                context.find = INVALID_HANDLE_VALUE;
#elif defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && (__MAC_OS_X_VERSION_MIN_REQUIRED >= 101000)
                context.find = -1;
#else
                context.find = nullptr;
#endif
            }

            ~state_type()
            {
                plat_dir_close(&context);
            }

            bool open(const char *directory)
            {
                return plat_dir_open(&context, directory) != 0;
            }

            plat_dir_t      context {};
            directory_entry current;
            bool            at_end = false;
        };

        void advance()
        {
            if (!state_ || state_->at_end)
                return;

            if (!plat_dir_read(&state_->context)) {
                state_->at_end = true;
                state_.reset();
                return;
            }

            state_->current = directory_entry(path(plat_dir_get_path(&state_->context)).lexically_normal(),
                                              detail::classify_directory_entry(&state_->context));
        }

        std::shared_ptr<state_type> state_;
    };


    inline path current_path(std::error_code &error)
    {
        std::vector<char> buffer(4096, '\0');
        plat_getcwd(buffer.data(), static_cast<int>(buffer.size()));
        if (buffer[0] == '\0') {
            error = std::make_error_code(std::errc::io_error);
            return {};
        }

        error.clear();
        return path(buffer.data()).lexically_normal();
    }

    inline file_status status(const path &path_value, std::error_code &error)
    {
        if (path_value.empty()) {
            error.clear();
            return file_status(file_type::not_found);
        }

        std::vector<char> buffer(path_value.generic_string().begin(), path_value.generic_string().end());
        buffer.push_back('\0');
        if (plat_dir_check(buffer.data())) {
            error.clear();
            return file_status(file_type::directory);
        }

        if (plat_file_check(buffer.data())) {
            error.clear();
            return file_status(file_type::regular);
        }

        error.clear();
        return file_status(file_type::not_found);
    }

    inline bool exists(const file_status &status_value)
    {
        return status_value.type() != file_type::none && status_value.type() != file_type::not_found;
    }

    inline bool is_directory(const file_status &status_value)
    {
        return status_value.type() == file_type::directory;
    }
};
