#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace quickxlsx::errors {

/** Base class for all library-reported runtime failures. */ class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
/** Input path does not exist. */ class FileNotFound : public Error { public: using Error::Error; };
/** Input or output path cannot be opened due to permissions. */ class FilePermissionDenied : public Error { public: using Error::Error; };
/** Container or document structure is corrupted. */ class FileCorrupted : public Error { public: using Error::Error; };
/** CSV syntax or delimiter is invalid. */ class CSVParseError : public Error { public: using Error::Error; };

/** XLSX parsing failure with optional XML-part location metadata. */
class XLSXParseError : public Error {
public:
    /** Constructs an error and takes ownership of message and XML-part name. */
    XLSXParseError(std::string message, std::string xml_file = {}, std::size_t line = 0, std::size_t column = 0)
        : Error(std::move(message)), xml_file_(std::move(xml_file)), line_(line), column_(column) {}
    /** Returns the XML-part name; the reference is valid for this exception's lifetime. */ [[nodiscard]] const std::string& xml_file() const noexcept { return xml_file_; }
    /** Returns the reported XML line, or zero when unavailable. */ [[nodiscard]] std::size_t line() const noexcept { return line_; }
    /** Returns the reported XML column, or zero when unavailable. */ [[nodiscard]] std::size_t column() const noexcept { return column_; }
private:
    std::string xml_file_;
    std::size_t line_;
    std::size_t column_;
};

/** A required document element is absent. */ class MissingRequiredElement : public Error { public: using Error::Error; };
/** Input contains invalid UTF-8. */ class InvalidUTF8 : public Error { public: using Error::Error; };
/** Character encoding conversion failed. */ class EncodingConversionFailed : public Error { public: using Error::Error; };
/** General input/output failure. */ class IOError : public Error { public: using Error::Error; };
/** Output could not be written or committed. */ class WriteError : public Error { public: using Error::Error; };
/** Input could not be read or a Reader is closed. */ class ReadError : public Error { public: using Error::Error; };
/** Worksheet name is invalid or duplicated. */ class InvalidWorksheetName : public Error { public: using Error::Error; };
/** Requested worksheet does not exist. */ class WorksheetNotFound : public Error { public: using Error::Error; };
/** Requested sparse row does not exist. */ class InvalidRowIndex : public Error { public: using Error::Error; };
/** Requested sparse column does not exist. */ class InvalidColumnIndex : public Error { public: using Error::Error; };
/** A1 notation, range bounds, or relative coordinates are invalid. */ class InvalidRange : public Error { public: using Error::Error; };
/** Requested format or operation is not supported by this build. */ class UnsupportedFeature : public Error { public: using Error::Error; };
/** Internal invariant failed. */ class InternalError : public Error { public: using Error::Error; };
/** ZIP container operation failed. */ class ZipError : public Error { public: using Error::Error; };
/** XML syntax or processing failed. */ class XMLParseError : public Error { public: using Error::Error; };

} // namespace quickxlsx::errors
