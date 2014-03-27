/**
 * This file is part of the CernVM File System.
 *
 * Implements a simple cvmfs shell which can be used instead of a full
 * mount point.
 */

#include "cvmfs_config.h"
#include "shell.h"

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "logging.h"
#include "util.h"

using namespace std;  // NOLINT
using namespace loader;  // NOLINT

namespace shell {

Shell::Shell() {
  cwd_ = "";
}


int Shell::Enter(struct loader::LoaderExports *loader_exports,
                 struct loader::CvmfsExports *cvmfs_exports)
{
  loader_exports_ = loader_exports;
  cvmfs_exports_ = cvmfs_exports;
  while (Prompt()) { }
  return 0;
}


bool Shell::Prompt() {
  LogCvmfs(kLogShell, kLogStdout | kLogNoLinebreak,
           "[%s]: ", loader_exports_->repository_name.c_str());

  string line;
  bool contd = GetLineFile(stdin, &line);
  if (!contd) {
    LogCvmfs(kLogShell, kLogStdout, "");
    return false;
  }

  if (line.substr(0, 5) == "stat ") {
    string path = line.substr(5);
    struct stat info;
    int retval = cvmfs_exports_->simple_ops->fnStat(path, &info);
  } else if (line == "ls") {
    Ls();
  } else if ((line == "quit") || (line == "exit") || (line == "q")) {
    return false;
  }

  return true;
}

void Shell::Ls() {

}

}  // namespace shell
