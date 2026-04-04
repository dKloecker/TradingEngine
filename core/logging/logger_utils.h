//
// Created by Dominic Kloecker on 04/04/2026.
//

#ifndef TRADING_LOGGER_UTILS_H
#define TRADING_LOGGER_UTILS_H

namespace dsl {
// TODO: Come up with better implementation for this
inline void write_log(std::ofstream &file, const std::string &format, const LogRecord &record) {
    const char *p = format.c_str();
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            switch (*(p + 1)) {
                case 'T': {
                    // TODO: Add Time
                    file << "TIMESTAMP";
                    break;
                }
                case 'L': {
                    file << to_string(record.level);
                    break;
                }
                case 'm': {
                    file.write(record.message, record.message_length);
                    break;
                }
                case 'f': {
                    file << record.location.file_name();
                    break;
                }
                case 'l': {
                    file << record.location.line();
                    break;
                }
                case 'F': {
                    file << record.location.function_name();
                    break;
                }
                case '%': {
                    file << '%';
                    break;
                }
                default: {
                    file << '%' << *(p + 1);
                    break;
                }
            }
            p += 2;
        } else {
            file << *p;
            ++p;
        }
    }
    file << '\n';
}
}
#endif //TRADING_LOGGER_UTILS_H
