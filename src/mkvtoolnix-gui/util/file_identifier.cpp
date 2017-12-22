#include "common/common_pch.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStringList>

#include "common/checksums/base_fwd.h"
#include "common/json.h"
#include "common/qt.h"
#include "common/strings/editing.h"
#include "common/strings/formatting.h"
#include "mkvtoolnix-gui/merge/mux_config.h"
#include "mkvtoolnix-gui/merge/source_file.h"
#include "mkvtoolnix-gui/util/cache.h"
#include "mkvtoolnix-gui/util/file_identifier.h"
#include "mkvtoolnix-gui/util/json.h"
#include "mkvtoolnix-gui/util/process.h"
#include "mkvtoolnix-gui/util/settings.h"

namespace mtx { namespace gui { namespace Util {

class FileIdentifierPrivate {
  friend class FileIdentifier;

  bool m_succeeded{}, m_jsonParsingFailed{};
  int m_exitCode{};
  QStringList m_output;
  QString m_fileName, m_errorTitle, m_errorText;
  mtx::gui::Merge::SourceFilePtr m_file;

  explicit FileIdentifierPrivate(QString const &fileName)
    : m_fileName{fileName}
  {
  }
};

using namespace mtx::gui;

FileIdentifier::FileIdentifier(QString const &fileName)
  : p_ptr{new FileIdentifierPrivate{QDir::toNativeSeparators(fileName)}}
{
}

FileIdentifier::~FileIdentifier() {
}

bool
FileIdentifier::identify() {
  auto p         = p_func();
  p->m_succeeded = false;

  if (p->m_fileName.isEmpty())
    return false;

  if (retrieveResultFromCache()) {
    setDefaults();
    return p->m_succeeded;
  }

  auto &cfg = Settings::get();

  auto args = QStringList{} << "--output-charset" << "utf-8" << "--identification-format" << "json" << "--identify" << p->m_fileName;

  addProbeRangePercentageArg(args, cfg.m_probeRangePercentage);

  if (cfg.m_defaultAdditionalMergeOptions.contains(Q("keep_last_chapter_in_mpls")))
    args << "--engage" << "keep_last_chapter_in_mpls";

  auto process  = Process::execute(cfg.actualMkvmergeExe(), args);
  p->m_exitCode = process->process().exitCode();

  if (process->hasError()) {
    setError(QY("Error executing mkvmerge"), QY("The mkvmerge executable was not found."));
    return false;
  }

  p->m_output    = process->output();
  p->m_succeeded = parseOutput();

  storeResultInCache();

  setDefaults();

  return p->m_succeeded;
}

QString const &
FileIdentifier::fileName()
  const {
  return p_func()->m_fileName;
}

void
FileIdentifier::setFileName(QString const &fileName) {
  p_func()->m_fileName = QDir::toNativeSeparators(fileName);
}

QString const &
FileIdentifier::errorTitle()
  const {
  return p_func()->m_errorTitle;
}

QString const &
FileIdentifier::errorText()
  const {
  return p_func()->m_errorText;
}

void
FileIdentifier::setError(QString const &errorTitle,
                         QString const &errorText) {
  auto p          = p_func();
  p->m_errorTitle = errorTitle;
  p->m_errorText  = errorText;
}

int
FileIdentifier::exitCode()
  const {
  return p_func()->m_exitCode;
}

QStringList const &
FileIdentifier::output()
  const {
  return p_func()->m_output;
}

Merge::SourceFilePtr const &
FileIdentifier::file()
  const {
  return p_func()->m_file;
}

bool
FileIdentifier::parseOutput() {
  auto p    = p_func();
  auto root = QVariantMap{};

  try {
    auto doc = mtx::json::parse(to_utf8(p->m_output.join("\n")));
    root     = nlohmannJsonToVariant(doc).toMap();

  } catch (std::exception const &ex) {
    p->m_jsonParsingFailed = true;
    setError(QY("Error executing mkvmerge"), QY("The JSON output generated by mkvmerge could not be parsed (parser's error message: %1).").arg(Q(ex.what())));
    return false;
  }

  auto container = root.value("container").toMap();

  if (!container.value("recognized").toBool()) {
    setError(QY("Unrecognized file format"), QY("The file was not recognized as a supported format (exit code: %1).").arg(p->m_exitCode));
    return false;
  }

  if (!container.value("supported").toBool()) {
    setError(QY("Unsupported file format"), QY("The file is an unsupported container format (%1).").arg(container.value("type").toString()));
    return false;
  }

  p->m_file                         = std::make_shared<Merge::SourceFile>(p->m_fileName);
  p->m_file->m_probeRangePercentage = Settings::get().m_probeRangePercentage;

  parseContainer(container);

  for (auto const &val : root.value("tracks").toList())
    parseTrack(val.toMap());

  for (auto const &val : root.value("attachments").toList())
    parseAttachment(val.toMap());

  for (auto const &val : root.value("chapters").toList())
    parseChapters(val.toMap());

  for (auto const &val : root.value("global_tags").toList())
    parseGlobalTags(val.toMap());

  for (auto const &val : root.value("track_tags").toList())
    parseTrackTags(val.toMap());

  return p->m_file->isValid();
}

// "content_type": "text/plain",
// "description": "",
// "file_name": "vde.srt",
// "id": 1,
// "properties": {
//   "uid": 14629734976961512390
// },
// "size": 1274
void
FileIdentifier::parseAttachment(QVariantMap const &obj) {
  auto p                         = p_func();
  auto track                     = std::make_shared<Merge::Track>(p->m_file.get(), Merge::Track::Attachment);
  track->m_properties            = obj.value("properties").toMap();
  track->m_id                    = obj.value("id").toULongLong();
  track->m_codec                 = obj.value("content_type").toString();
  track->m_size                  = obj.value("size").toULongLong();
  track->m_attachmentDescription = obj.value("description").toString();
  track->m_name                  = QDir::toNativeSeparators(obj.value("file_name").toString());

  p->m_file->m_attachedFiles << track;
}

// "num_entries": 5
void
FileIdentifier::parseChapters(QVariantMap const &obj) {
  auto p        = p_func();
  auto track    = std::make_shared<Merge::Track>(p->m_file.get(), Merge::Track::Chapters);
  track->m_size = obj.value("num_entries").toULongLong();

  p->m_file->m_tracks << track;
}

// "properties": {
//   "container_type": 17,
//   "duration": 71255000000,
//   "is_providing_timestamps": true,
//   "segment_uid": "a93dd71320097620bc002ac740ac4b50"
// },
// "recognized": true,
// "supported": true,
// "type": "Matroska"
void
FileIdentifier::parseContainer(QVariantMap const &obj) {
  auto p                        = p_func();
  p->m_file->m_properties       = obj.value("properties").toMap();
  p->m_file->m_type             = static_cast<file_type_e>(p->m_file->m_properties.value("container_type").toUInt());
  p->m_file->m_isPlaylist       = p->m_file->m_properties.value("playlist").toBool();
  p->m_file->m_playlistDuration = p->m_file->m_properties.value("playlist_duration").toULongLong();
  p->m_file->m_playlistSize     = p->m_file->m_properties.value("playlist_size").toULongLong();
  p->m_file->m_playlistChapters = p->m_file->m_properties.value("playlist_chapters").toULongLong();

  if (p->m_file->m_isPlaylist)
    for (auto const &fileName : p->m_file->m_properties.value("playlist_file").toStringList())
      p->m_file->m_playlistFiles << QFileInfo{fileName};

  auto otherFiles = p->m_file->m_properties.value("other_file").toStringList();
  for (auto &fileName : otherFiles) {
    auto additionalPart              = std::make_shared<Merge::SourceFile>(fileName);
    additionalPart->m_additionalPart = true;
    additionalPart->m_appendedTo     = p->m_file.get();
    p->m_file->m_additionalParts       << additionalPart;
  }
}

// "num_entries": 5
void
FileIdentifier::parseGlobalTags(QVariantMap const &obj) {
  auto p        = p_func();
  auto track    = std::make_shared<Merge::Track>(p->m_file.get(), Merge::Track::GlobalTags);
  track->m_size = obj.value("num_entries").toULongLong();

  p->m_file->m_tracks << track;
}

// "num_entries": 7,
// "track_id": 0
void
FileIdentifier::parseTrackTags(QVariantMap const &obj) {
  auto p        = p_func();
  auto track    = std::make_shared<Merge::Track>(p->m_file.get(), Merge::Track::Tags);
  track->m_id   = obj.value("track_id").toULongLong();
  track->m_size = obj.value("num_entries").toULongLong();

  p->m_file->m_tracks << track;
}

// "codec": "AAC",
// "id": 3,
// "properties": {
//   "audio_channels": 2,
//   "audio_sampling_frequency": 48000,
//   "codec_id": "A_AAC",
//   "uid": 15551853204593941928
// },
// "type": "audio"
void
FileIdentifier::parseTrack(QVariantMap const &obj) {
  auto p              = p_func();
  auto typeStr        = obj.value("type").toString();
  auto type           = typeStr == "audio"     ? Merge::Track::Audio
                      : typeStr == "video"     ? Merge::Track::Video
                      : typeStr == "subtitles" ? Merge::Track::Subtitles
                      :                          Merge::Track::Buttons;
  auto track          = std::make_shared<Merge::Track>(p->m_file.get(), type);
  track->m_id         = obj.value("id").toULongLong();
  track->m_codec      = obj.value("codec").toString();
  track->m_properties = obj.value("properties").toMap();

  p->m_file->m_tracks << track;
}

void
FileIdentifier::addProbeRangePercentageArg(QStringList &args,
                                           double probeRangePercentage) {
  if (probeRangePercentage <= 0)
    return;

  auto integerPart = static_cast<unsigned int>(std::round(probeRangePercentage * 100)) / 100;
  auto decimalPart = static_cast<unsigned int>(std::round(probeRangePercentage * 100)) % 100;

  if (integerPart >= 100)
    return;

  if (   (integerPart != 0)
      || (   (decimalPart !=  0)
          && (decimalPart != 30)))
    args << "--probe-range-percentage" << Q(boost::format("%1%.%|2$02d|") % integerPart % decimalPart);
}

QString
FileIdentifier::cacheKey()
  const {
  auto p        = p_func();
  auto fileName = to_utf8(QDir::toNativeSeparators(p->m_fileName));

  return Q(to_hex(mtx::checksum::calculate(mtx::checksum::algorithm_e::md5, &fileName[0], fileName.length()), true));
}

QHash<QString, QVariant>
FileIdentifier::cacheProperties()
  const {
  auto p                                = p_func();
  auto info                             = QFileInfo{p->m_fileName};
  auto properties                       = QHash<QString, QVariant>{};

  properties[Q("fileName")]             = QDir::toNativeSeparators(p->m_fileName);
  properties[Q("fileSize")]             = info.size();
  properties[Q("fileModificationTime")] = info.lastModified().toMSecsSinceEpoch();

  return properties;
}

void
FileIdentifier::storeResultInCache()
  const {
  auto p = p_func();

  if (p->m_jsonParsingFailed)
    return;

  auto settings = Cache::create(cacheCategory(), cacheKey(), cacheProperties());

  settings->beginGroup("identifier");
  settings->setValue("succeeded",  p->m_succeeded);
  settings->setValue("exitCode",   p->m_exitCode);
  settings->setValue("output",     p->m_output);
  settings->setValue("errorTitle", p->m_errorTitle);
  settings->setValue("errorText",  p->m_errorText);
  settings->endGroup();

  if (p->m_succeeded) {
    settings->beginGroup("sourceFile");
    p->m_file->saveSettings(*settings);
    settings->endGroup();
  }

  settings->save();
}

bool
FileIdentifier::retrieveResultFromCache() {
  auto p        = p_func();
  auto settings = Cache::fetch(cacheCategory(), cacheKey(), cacheProperties());

  if (!settings) {
    qDebug() << "FileIdentifier::retrievePositiveResultFromCache: false 1";
    return false;
  }

  try {
    qDebug() << "FileIdentifier::retrievePositiveResultFromCache: cached content fetched, loading settings from it";

    settings->beginGroup("identifier");
    p->m_succeeded  = settings->value("succeeded").toBool();
    p->m_exitCode   = settings->value("exitCode").toInt();
    p->m_output     = settings->value("output").toStringList();
    p->m_errorTitle = settings->value("errorTitle").toString();
    p->m_errorText  = settings->value("errorText").toString();
    settings->endGroup();

    if (p->m_succeeded) {
      settings->beginGroup("sourceFile");

      QHash<qulonglong, Merge::SourceFile *> objectIDToSourceFile;
      QHash<qulonglong, Merge::Track *> objectIDToTrack;
      Merge::MuxConfig::Loader l{*settings, objectIDToSourceFile, objectIDToTrack};

      p->m_file = std::make_shared<Merge::SourceFile>(p->m_fileName);
      p->m_file->loadSettings(l);
      p->m_file->fixAssociations(l);

      settings->endGroup();
    }

    qDebug() << "FileIdentifier::retrievePositiveResultFromCache: loaded";

    return true;

  } catch (Merge::InvalidSettingsX &) {
    qDebug() << "FileIdentifier::retrievePositiveResultFromCache: InvalidSettingsX";
  }

  qDebug() << "FileIdentifier::retrievePositiveResultFromCache: false 2, removing cache file";

  settings.reset();
  Cache::remove(cacheCategory(), cacheKey());

  *p_ptr = FileIdentifierPrivate{QDir::toNativeSeparators(p->m_fileName)};

  return false;
}

void
FileIdentifier::cleanAllCacheFiles() {
  Cache::cleanAllCacheFilesForCategory(cacheCategory());
}

QString
FileIdentifier::cacheCategory() {
  return Q("fileIdentifier");
}

void
FileIdentifier::setDefaults() {
  auto p = p_func();

  if (!p->m_file)
    return;

  p->m_file->setDefaults();
  p->m_file->setupProgramMapFromProperties();
}

}}}
