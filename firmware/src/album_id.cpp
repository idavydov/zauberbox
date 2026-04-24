#include "album_id.h"

bool parseAlbumSelectorPayload(const char *payload, String *albumId) {
    if (!payload || !albumId) {
        return false;
    }

    static constexpr char kPrefix[] = "file://";
    const String payloadString(payload);
    if (!payloadString.startsWith(kPrefix)) {
        return false;
    }

    String candidate = payloadString.substring(strlen(kPrefix));
    if (candidate.isEmpty()) {
        return false;
    }

    if (candidate.endsWith("/")) {
        candidate.remove(candidate.length() - 1);
    }
    if (candidate.isEmpty()) {
        return false;
    }

    for (size_t i = 0; i < candidate.length(); i++) {
        if (!isDigit(candidate.charAt(i))) {
            return false;
        }
    }

    *albumId = candidate;
    return true;
}
