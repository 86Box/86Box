#include <cstdlib>
#include <cstdio>
#include <vector>
#include <sstream>
#include <atomic>

#include <regex>

extern "C"
{
#include <86box/86box.h>
#include <86box/plat.h>    

extern int ini_detect_bom(const char *fn);
extern ssize_t local_getline(char **buf, size_t *bufsiz, FILE *fp);
}

// https://stackoverflow.com/a/64886763
static std::vector<std::string> split(const std::string str, const std::string regex_str)
{
    std::regex regexz(regex_str);
    std::vector<std::string> list(std::sregex_token_iterator(str.begin(), str.end(), regexz, -1),
                                  std::sregex_token_iterator());
    return list;
}

extern "C"
{
    bool parse_edid_decode_file(const char* path, uint8_t* out, ssize_t* size_out) 
    {
        std::regex regexLib("^([a-f0-9]{32}|[a-f0-9 ]{47})$", std::regex_constants::egrep);
        FILE* file = NULL;
        try {
            bool bom = ini_detect_bom(path);
            {
                // First check for "edid-decode (hex)" string.
                file = plat_fopen(path, "rb");
                if (file) {
                    std::string str;
                    ssize_t size;
                    if (!fseek(file, 0, SEEK_END)) {
                        size = ftell(file);
                        if (size != -1) {
                            str.resize(size);
                        }
                        fseek(file, 0, SEEK_SET);
                        auto read = fread((void*)str.data(), 1, size, file);
                        str.resize(read);
                        fclose(file);
                        file = NULL;

                        if (str.size() == 0) {
                            return false;
                        }

                        if (str.find("edid-decode (hex):") == std::string::npos) {
                            return false;
                        }
                    }
                } else {
                    return false;
                }
            }
            file = plat_fopen(path, "rb");
            if (file) {
                size_t size = 0;
                std::string edid_decode_text;
                fseek(file, 0, SEEK_END);
                size = ftell(file);
                fseek(file, 0, SEEK_SET);
                if (bom) {
                    fseek(file, 3, SEEK_SET);
                    size -= 3;
                }
                edid_decode_text.resize(size);
                auto err = fread((void*)edid_decode_text.data(), size, 1, file);
                fclose(file);
                file = NULL;
                if (err == 0) {
                    return false;
                }
                std::istringstream isstream(edid_decode_text);
                std::string line;
                std::string edid;
                while (std::getline(isstream, line)) {
                    if (line[line.size() - 1] == '\r') {
                        line.resize(line.size() - 1);
                    }
                    std::smatch matched;
                    if (std::regex_match(line, matched, regexLib)) {
                        edid.append(matched.str() + " ");
                    }
                }
                if (edid.size() >= 3) {
                    edid.resize(edid.size() - 1);
                    auto vals = split(edid, "\\s+");
                    if (vals.size()) {
                        *size_out = vals.size();
                        if (vals.size() > 256)
                            return false;
                        for (size_t i = 0; i < vals.size(); i++) {
                            out[i] = (uint8_t)std::strtoul(&vals[i][0], nullptr, 16);
                        }
                        return true;
                    }
                }
            }

            return false;
        } catch (std::bad_alloc&) {
            if (file) {
                fclose(file);
                file = NULL;
            }
            return false;
        }
        return false;
    }
}