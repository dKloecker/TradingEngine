//
// Created by Dominic Kloecker on 04/04/2026.
//

#ifndef TRADING_LOGGER_UTILS_H
#define TRADING_LOGGER_UTILS_H

namespace dsl {
// TODO: Refactor this into cleaner separate implementation
inline void write_log(std::ostream &out, const std::string &format, const LogRecord &record) {
    const char *p = format.c_str();
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            switch (*(p + 1)) {
                case 'T': {
                    const auto time = std::chrono::system_clock::to_time_t(record.time_stamp);
                    const auto us   = std::chrono::duration_cast<std::chrono::microseconds>(
                                        record.time_stamp.time_since_epoch()) % std::chrono::seconds(1);

                    std::tm buf{};
                    gmtime_r(&time, &buf);

                    char       time_str[32];
                    const auto len = std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &buf);
                    out.write(time_str, len);
                    out << '.' << std::setfill('0') << std::setw(6) << us.count();
                    break;
                }
                case 'L': {
                    out << to_string(record.level);
                    break;
                }
                case 'm': {
                    out.write(record.message, record.message_length);
                    break;
                }
                case 'f': {
                    out << record.location.file_name();
                    break;
                }
                case 'l': {
                    out << record.location.line();
                    break;
                }
                case 'F': {
                    // TODO: Better way of doing this? Output is otherwise too long at the moment
                    const char *file_name = record.location.file_name();
                    if (const char *last_slash = std::strrchr(file_name, '/')) {
                        file_name = last_slash + 1;
                    }
                    out << file_name;
                    break;
                }
                case '%': {
                    out << '%';
                    break;
                }
                default: {
                    out << '%' << *(p + 1);
                    break;
                }
            }
            p += 2;
        } else {
            out << *p;
            ++p;
        }
    }
    out << '\n';
}
}
#endif //TRADING_LOGGER_UTILS_H
