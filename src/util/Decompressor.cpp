// Copyright 2024, University of Freiburg
// Authors: Nicolas von Trott <nicolasvontrott@gmail.com>.

// This file is part of osm-live-updates.
//
// osm-live-updates is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// osm-live-updates is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with osm-live-updates.  If not, see <https://www.gnu.org/licenses/>.

#include "util/Decompressor.h"

#include <string>
#include <osmium/io/gzip_compression.hpp>

namespace olu::util {

// ____________________________________________________________________________
std::string Decompressor::read(const std::string& path) {
    const int fd = osmium::io::detail::open_for_reading(path);

    size_t size = 0;
    std::string all;
    osmium::io::GzipDecompressor decompressor { fd };
    for (std::string data = decompressor.read(); !data.empty(); data = decompressor.read()) {
        size += data.size();
        all += data;
    }

    decompressor.close();
    return all;
}

} //namespace olu::util