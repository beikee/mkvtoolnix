/*
  mkvmerge -- utility for splicing together matroska files
  from component media subtypes

  Distributed under the GPL v2
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html

  Exceptios for the I/O callback class

  Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include <system_error>

namespace mtx { namespace mm_io {

std::error_code make_error_code();

class exception: public mtx::exception {
private:
  std::error_code m_error_code;

public:
  exception(std::error_code const &error_code = std::error_code())
    : mtx::exception()
    , m_error_code{error_code}
  {
  }

  virtual const char *what() const noexcept {
    return "unspecified I/O error";
  }

  std::error_code const &code() const {
    return m_error_code;
  }

  virtual std::string error() const noexcept;
};

class end_of_file_x: public exception {
public:
  end_of_file_x(std::error_code const &error_code = std::error_code()) : exception(error_code) {}

  virtual char const *what() const noexcept {
    return "end of file error";
  }
};

class seek_x: public exception {
public:
  seek_x(std::error_code const &error_code = std::error_code()) : exception(error_code) {}

  virtual char const *what() const noexcept {
    return "seek in file error";
  }
};

class read_write_x: public exception {
public:
  read_write_x(std::error_code const &error_code = std::error_code()) : exception(error_code) {}

  virtual char const *what() const noexcept {
    return "reading from/writing to the file error";
  }
};

class open_x: public exception {
public:
  open_x(std::error_code const &error_code = std::error_code()) : exception(error_code) {}

  virtual char const *what() const noexcept {
    return "open file error";
  }
};

class wrong_read_write_access_x: public exception {
public:
  wrong_read_write_access_x(std::error_code const &error_code = std::error_code()) : exception(error_code) {}

  virtual char const *what() const noexcept {
    return "write operation to read-only file or vice versa";
  }
};

class insufficient_space_x: public exception {
public:
  insufficient_space_x(std::error_code const &error_code = std::error_code()) : exception(error_code) {}

  virtual char const *what() const noexcept {
    return "insufficient space for write operation";
  }
};

class create_directory_x: public exception {
protected:
  std::string m_path;
public:
  create_directory_x(std::string const &path,
                     std::error_code const &error_code = std::error_code())
    : exception(error_code)
    , m_path{path}
  {
  }

  virtual ~create_directory_x() noexcept { }

  virtual char const *what() const noexcept {
    return "create_directory() failed";
  }
  virtual std::string error() const noexcept {
    return fmt::format(Y("Creating directory '{0}' failed: {1}"), m_path, code().message());
  }
};

namespace text {
class exception: public mtx::mm_io::exception {
public:
  exception(std::error_code const &error_code = std::error_code()) : mtx::mm_io::exception(error_code) {}

  virtual char const *what() const noexcept {
    return "unspecified text I/O error";
  }
};

class invalid_utf8_char_x: public exception {
protected:
  char m_first_char;
public:
  invalid_utf8_char_x(char first_char, std::error_code const &error_code = std::error_code())
    : exception(error_code)
    , m_first_char{first_char}
  {
  }

  virtual char const *what() const noexcept {
    return "invalid UTF-8 char";
  }

  virtual std::string error() const noexcept {
    return fmt::format(Y("Invalid UTF-8 char. First byte: 0x{0:02x}"), static_cast<unsigned int>(m_first_char));
  }
};

inline std::ostream &
operator <<(std::ostream &out,
            exception const &ex) {
  out << ex.error();
  return out;
}

}}}
