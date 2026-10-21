#pragma once
#include "Decode/AudioPcmDecoder.h"

void DecodeMp3(const std::filesystem::path& path, std::stop_token stop,
               const AudioPcmConsumer& consume);
void DecodeVorbis(const std::filesystem::path& path, std::stop_token stop,
                  const AudioPcmConsumer& consume);
void DecodeMediaFoundation(const std::filesystem::path& path, std::stop_token stop,
                           const AudioPcmConsumer& consume);
