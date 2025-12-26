#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <regex>

namespace fs = std::filesystem;

struct media {
    fs::path path;
    int64_t timestamp_ms; // unix time, important for ranking photos when taken within the same second 
};

std::string run_command(const std::string& cmd) {
    std::string result;
    char buffer[256];
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// parse iso8601 timestamp
int64_t parse_iso8601_to_millis(const std::string& dt) {
    // handle both "2025-12-25T16:07:57.123-0700" and "2025-12-25T16:07:57.-0700" (empty fraction)
    std::regex re(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d*))?([+-]\d{2})?(\d{2})?)");
    std::smatch match;
    if (!std::regex_match(dt, match, re)) return 0;

    int year = std::stoi(match[1]);
    int month = std::stoi(match[2]);
    int day = std::stoi(match[3]);
    int hour = std::stoi(match[4]);
    int min = std::stoi(match[5]);
    int sec = std::stoi(match[6]);

    // extract milliseconds from fractional seconds (if present in EXIF)
    // this provides ranking precision when rapidly taking photos in burst mode.
    // if camera doesn't write milliseconds to EXIF, files taken in the same second
    // will have identical timestamps and be sorted by filesystem order (usually filename).
    // genuinely the only way i know how to solve this without crashing out or overcomplicating stuff.
    int millis = 0;
    if (match[7].matched) {
        std::string frac = match[7];
        if (!frac.empty()) {  // only parse if there are actual digits
            while (frac.size() < 3) frac += '0';
            millis = std::stoi(frac.substr(0,3));
        }
    }

    int tz_offset_sec = 0;
    if (match[8].matched && match[9].matched) {
        int tz_hour = std::stoi(match[8]);
        int tz_min = std::stoi(match[9]);
        tz_offset_sec = tz_hour * 3600 + (tz_hour >=0 ? tz_min*60 : -tz_min*60);
    }

    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;

    std::time_t epoch = timegm(&t) - tz_offset_sec; 
    return static_cast<int64_t>(epoch) * 1000 + millis;
}

// extract the first valid timestamp from exiftool output
int64_t get_file_timestamp(const fs::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool use_quicktime_api = (ext == ".mov" || ext == ".mp4" || ext == ".m4v");

    std::string cmd = "exiftool ";
    if (use_quicktime_api) cmd += "-api QuickTimeUTC ";
    cmd += "-DateTimeOriginal -CreateDate -ModifyDate -MediaCreateDate -TrackCreateDate -FileModifyDate "
           "-s -s -s -d \"%Y-%m-%dT%H:%M:%S.%f%z\" \"" + file.string() + "\"";

    std::string output = run_command(cmd);
    if (output.empty()) return 0;

    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        int64_t ts = parse_iso8601_to_millis(line);
        if (ts > 0) return ts;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    bool dry_run = false;
    std::vector<std::string> args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dry-run") dry_run = true;
        else args.push_back(arg);
    }

    if (args.size() < 2) {
        std::cout << "usage: imgsort [--dry-run] <directory> <base name>\n";
        return 1;
    }

    fs::path dir = fs::absolute(args[0]);
    std::string base_name = args[1];

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cout << "directory does not exist\n";
        return 1;
    }

    if (dry_run) std::cout << "dry run mode enabled, no files will be renamed\n\n";

    std::vector<media> items;

    std::vector<std::string> photo_exts = {".jpg", ".jpeg", ".png", ".heic", ".dng", ".nef", ".cr2", ".cr3", ".arw", ".orf", ".raf"};
    std::vector<std::string> video_exts = {".mov", ".mp4", ".m4v", ".avi", ".mts", ".m2ts", ".3gp", ".mkv"};

    std::cout << "scanning directory for media files...\n";

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool is_media = (std::find(photo_exts.begin(), photo_exts.end(), ext) != photo_exts.end()) ||
                        (std::find(video_exts.begin(), video_exts.end(), ext) != video_exts.end());
        if (!is_media) continue;

        std::cout << "reading metadata: " << entry.path().filename().string() << std::endl;

        int64_t ts_ms = get_file_timestamp(entry.path());
        if (ts_ms == 0) {
            std::cout << "warning: " << entry.path().filename().string()
                      << " has no valid exif timestamp, using file modification date as fallback\n";
            ts_ms = fs::last_write_time(entry.path()).time_since_epoch().count() / 1000000;
        }

        items.push_back({entry.path(), ts_ms});
    }

    if (items.empty()) {
        std::cout << "no media found\n";
        return 1;
    }

    std::cout << "\nsorting " << items.size() << " files by timestamp...\n\n";

    // sort all items by timestamp ascending
    std::sort(items.begin(), items.end(),
              [](const media& a, const media& b) { return a.timestamp_ms < b.timestamp_ms; });

    int total_files = static_cast<int>(items.size());
    int width = std::max(3, static_cast<int>(std::to_string(total_files).length()));
    int counter = 1;

    for (const auto& m : items) {
        std::ostringstream ss;
        ss << std::setw(width) << std::setfill('0') << counter;
        std::string num_str = ss.str();

        fs::path new_name = base_name + "_" + num_str + m.path.extension().string();
        fs::path new_path = dir / new_name;

        if (dry_run) {
            std::cout << "[dry-run] " << m.path.filename().string()
                      << " -> " << new_name.string() << "\n";
        } else {
            if (fs::exists(new_path)) {
                std::cout << "file exists, skipping: " << new_name.string() << "\n";
            } else {
                fs::rename(m.path, new_path);
                std::cout << "renamed: " << m.path.filename().string()
                          << " -> " << new_name.string() << "\n";
            }
        }

        counter++;
    }

    std::cout << "\nprocessed " << total_files << " items\n";
    return 0;
}

