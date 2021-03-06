/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_SWISSKNIFE_ZPIPE_H_
#define CVMFS_SWISSKNIFE_ZPIPE_H_

#include "swissknife.h"

namespace swissknife {

class CommandZpipe : public Command {
 public:
  ~CommandZpipe() { };
  std::string GetName() { return "zpipe"; };
  std::string GetDescription() {
    return "Compresses or decompresses a file using the DEFLATE algorithm.\n"
      "Input comes on stdin, output goes to stdout.";
  };
  ParameterList GetParams() {
    ParameterList result;
    result.push_back(Parameter('d', "decompress file", true, true));
    return result;
  }
  int Main(const ArgumentList &args);
};

}

#endif  // CVMFS_SWISSKNIFE_ZPIPE_H_
