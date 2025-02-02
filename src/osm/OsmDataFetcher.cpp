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

#include "osm/OsmDataFetcher.h"
#include "osm/WktHelper.h"
#include "config/Constants.h"
#include "util/URLHelper.h"
#include "util/HttpRequest.h"
#include "util/XmlReader.h"
#include "sparql/QueryWriter.h"
#include "util/Decompressor.h"

#include <string>
#include <vector>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <fstream>

namespace constants = olu::config::constants;

namespace olu::osm {

// _________________________________________________________________________________________________
OsmDataFetcher::OsmDataFetcher(olu::config::Config& config)
    : _config(config), _sparqlWrapper(olu::sparql::SparqlWrapper(config)) { }

// _________________________________________________________________________________________________
    OsmDatabaseState OsmDataFetcher::fetchLatestDatabaseState() const {
    OsmDatabaseState osmDatabaseState;

    // Build url for state file
    std::vector<std::string> pathSegments { };
    pathSegments.emplace_back(_config.osmDatabaseDirectoryPath);
    pathSegments.emplace_back(constants::OSM_DIFF_STATE_FILE + constants::TXT_EXTENSION);
    std::string url = util::URLHelper::buildUrl(pathSegments);

    // Get state file from osm server
    auto request = util::HttpRequest(util::GET, url);
    std::string readBuffer = request.perform();

    // Extract sequence number from state file
    boost::regex regexSeqNumber("sequenceNumber=(\\d+)");
    boost::smatch matchSeqNumber;
    if (boost::regex_search(readBuffer, matchSeqNumber, regexSeqNumber)) {
        std::string number = matchSeqNumber[1];
        osmDatabaseState.sequenceNumber = std::stoi(number);
    } else {
        throw OsmDataFetcherException(
                "Sequence number of latest database state could not be fetched");
    }

    // Extract timestamp from state file
    boost::regex regexTimestamp(
            R"(timestamp=([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}\\:[0-9]{2}\\:[0-9]{2}Z))");
    boost::smatch matchTimestamp;
    if (boost::regex_search(readBuffer, matchTimestamp, regexTimestamp)) {
        std::string timestamp = matchTimestamp[1];
        osmDatabaseState.timeStamp = timestamp;
    } else {
        throw OsmDataFetcherException(
                "Timestamp of latest database state could not be fetched");
    }

    return osmDatabaseState;
}

// _________________________________________________________________________________________________
    std::string OsmDataFetcher::fetchDiffWithSequenceNumber(int &sequenceNumber) {
    // Build url for diff file
    std::string sequenceNumberFormatted = util::URLHelper::formatSequenceNumberForUrl(sequenceNumber);
    std::string diffFilename = sequenceNumberFormatted + constants::OSM_CHANGE_FILE_EXTENSION + constants::GZIP_EXTENSION;
    std::vector<std::string> pathSegments;
    pathSegments.emplace_back(_config.osmDatabaseDirectoryPath);
    pathSegments.emplace_back(diffFilename);
    std::string url = util::URLHelper::buildUrl(pathSegments);

    // Get Diff file from server and write to cache file.
    std::string filePath = constants::DIFF_CACHE_FILE + std::to_string(sequenceNumber) + constants::OSM_CHANGE_FILE_EXTENSION + constants::GZIP_EXTENSION;
    auto request = util::HttpRequest(util::GET, url);

    auto response = request.perform();
    std::ofstream outputFile;
    outputFile.open (filePath);
    outputFile << response;
    outputFile.close();

    return filePath;
}

// _________________________________________________________________________________________________
std::string OsmDataFetcher::fetchNode(std::string &nodeId, bool extractNodeElement) {
    std::vector<std::string> pathSegments;
    pathSegments.emplace_back(constants::OSM_NODE_BASE_URL);
    pathSegments.emplace_back(nodeId);
    std::string url = util::URLHelper::buildUrl(pathSegments);

    auto request = util::HttpRequest(util::GET, url);
    std::string response = request.perform();

    if (extractNodeElement) {
        auto nodeElement = util::XmlReader::readNodeElement(response);
        return nodeElement;
    }

    return response;
}

std::vector<std::string> OsmDataFetcher::fetchNodes(const std::vector<std::string> &nodeIds) {
    std::vector<std::string> urls;
    for(const std::basic_string<char>& nodeId : nodeIds) {
        std::vector<std::string> pathSegments;
        pathSegments.emplace_back(constants::OSM_NODE_BASE_URL);
        pathSegments.emplace_back(nodeId);
        std::string url = util::URLHelper::buildUrl(pathSegments);
        urls.emplace_back(url);
    }

    auto responses = util::HttpRequest::multiPerform(urls);

    std::vector<std::string> nodeElements;
    for(const std::string& response : responses) {
        auto nodeElement = util::XmlReader::readNodeElement(response);
        nodeElements.emplace_back(nodeElement);
    }

    return nodeElements;
}

// _________________________________________________________________________________________________
std::vector<std::string> OsmDataFetcher::fetchNodeReferencesForWay(const boost::property_tree::ptree &way) {
    std::vector<std::string> referencedNodes;
    std::set<std::string> visitedNodes;

    for (const auto &child : way.get_child("")) {
        if (child.first != olu::config::constants::NODE_REFERENCE_TAG) {
            continue;
        }

        auto identifier = util::XmlReader::readAttribute(
                constants::NODE_REFERENCE_ATTRIBUTE,
                child.second);

        if (!visitedNodes.contains(identifier)) {
            visitedNodes.insert(identifier);
        }
    }

    std::vector<std::string> nodeIds(visitedNodes.begin(), visitedNodes.end());
    return fetchNodesFromSparql(nodeIds);
}


std::vector<std::string>
OsmDataFetcher::fetchNodesFromSparql(const std::vector<std::string> &nodeIds) {
    std::vector<std::string> dummyNodes;
    for(const std::string& nodeId : nodeIds) {
        auto query = olu::sparql::QueryWriter::writeQueryForNodeLocation(nodeId);
        _sparqlWrapper.setMethod(util::HttpMethod::GET);
        _sparqlWrapper.setQuery(query);
        _sparqlWrapper.setPrefixes(constants::PREFIXES_FOR_NODE_LOCATION);
        auto response = _sparqlWrapper.runQuery();
        boost::property_tree::ptree responseAsTree;
        olu::util::XmlReader::populatePTreeFromString(response, responseAsTree);

        std::string pointAsWkt;
        try {
            pointAsWkt = responseAsTree.get<std::string>(
                    constants::PATH_TO_SPARQL_RESULT_FOR_NODE_LOCATION);
        } catch (boost::property_tree::ptree_bad_path &e) {
            std::cerr << "Could not get location for node with id "
                          + nodeId
                          + " from sparql endpoint" << std::endl;
            throw OsmDataFetcherException(
                    ("Could not get location for node with id "
                    + nodeId
                    + " from sparql endpoint").c_str());
        }

        auto dummyNode = olu::osm::WktHelper::createDummyNodeFromPoint(nodeId, pointAsWkt);
        dummyNodes.emplace_back(dummyNode);
    }

    return dummyNodes;
}

} // namespace olu



