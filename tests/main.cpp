#define DOCTEST_CONFIG_IMPLEMENT
#include "main.h"
#include <term.h>
#include <fcntl.h>
#include <clocale>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <climits>
#include <termios.h>
#include <filesystem>
#include <langinfo.h>

static const char* datadir = NOTCURSES_SHARE;

auto testing_notcurses() -> struct notcurses* {
  notcurses_options nopts{};
  nopts.loglevel = NCLOGLEVEL_DEBUG;
  nopts.flags = NCOPTION_SUPPRESS_BANNERS;
  auto nc = notcurses_init(&nopts, nullptr);
  return nc;
}

auto find_data(const char* datum) -> char* {
  std::filesystem::path p = datadir;
  p /= datum;
  return strdup(p.c_str());
}

auto enforce_utf8() -> bool {
  char* enc = nl_langinfo(CODESET);
  if(!enc){
    return false;
  }
  if(strcmp(enc, "UTF-8")){
    return false;
  }
  return true;
}

static void
handle_opts(const char** argv){
  bool inarg = false;
  while(*argv){
    if(inarg){
      datadir = strdup(*argv);
      inarg = false;
    }else if(strcmp(*argv, "-p") == 0){
      inarg = true;
    }
    ++argv;
  }
}

// reset the terminal in the event of early exit (notcurses_init() presumably
// ran, but we don't have the notcurses struct to destroy. so just do it raw.
static void
reset_terminal(){
  int fd = open("/dev/tty", O_RDWR|O_CLOEXEC);
  if(fd >= 0){
    struct termios tios;
    if(tcgetattr(fd, &tios) == 0){
      tios.c_iflag |= INLCR;
      tios.c_lflag |= ISIG | ICANON | ECHO;
      tcsetattr(fd, TCSADRAIN, &tios);
    }
    char* str = tigetstr("sgr0");
    if(str != (char*)-1){
      printf("%s", str);
    }
    fflush(stdout);
    str = tigetstr("oc");
    if(str != (char*)-1){
      printf("%s", str);
    }
    fflush(stdout);
    close(fd);
  }
}

// from https://github.com/onqtam/doctest/blob/master/doc/markdown/commandline.md
class dt_removed {
  std::vector<const char*> vec;
public:
  dt_removed(const char** argv_in) {
      for(; *argv_in; ++argv_in)
          if(strncmp(*argv_in, "--dt-", strlen("--dt-")) != 0)
              vec.push_back(*argv_in);
      vec.push_back(nullptr);
  }

  auto argc() -> int { return static_cast<int>(vec.size()) - 1; }
  auto argv() -> const char** { return &vec[0]; }
};

auto main(int argc, const char **argv) -> int {
  if(!setlocale(LC_ALL, "")){
    std::cerr << "Couldn't set locale based on user preferences!" << std::endl;
    return EXIT_FAILURE;
  }
  if(getenv("TERM") == NULL){
    std::cerr << "TERM wasn't defined, exiting with success" << std::endl;
    return EXIT_SUCCESS;
  }
  doctest::Context context;

  context.setOption("order-by", "name");            // sort the test cases by their name

  context.applyCommandLine(argc, argv);

  // overrides
  context.setOption("no-breaks", true);             // don't break in the debugger when assertions fail

  dt_removed args(argv);
  handle_opts(argv);

  int res = context.run(); // run

  if(context.shouldExit()){ // important - query flags (and --exit) rely on the user doing this
    return res;             // propagate the result of the tests
  }

  // if we exited via REQUIRE(), we likely left the terminal in an invalid
  // state. go ahead and reset it manually.
  if(res){
    reset_terminal();
  }
  return res; // the result from doctest is propagated here as well
}
