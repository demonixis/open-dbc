#include "video_decoder.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

// AVPlayer advances on the main run loop, which SDL pumps with the window's events.

namespace dbc
{
    struct VideoDecoder::Impl
    {
        AVPlayer* Player = nil;
        AVPlayerItem* Item = nil;
        AVPlayerItemVideoOutput* Output = nil;
        id EndObserver = nil;
        // Set from AVFoundation's notification thread, hence shared with the observer block.
        std::shared_ptr<std::atomic<bool>> Ended = std::make_shared<std::atomic<bool>>(false);
        int Width = 0;
        int Height = 0;
        std::vector<std::uint8_t> Pixels;
        int FrameWidth = 0;
        int FrameHeight = 0;
    };

    namespace
    {
        // LOAD ANIMATION reports the size immediately, so the asynchronous loads are awaited here. Their
        // completion handlers run on AVFoundation's queues, never on the waiting thread.
        constexpr std::int64_t KLoadTimeoutNs = 10'000'000'000;

        bool Await(dispatch_semaphore_t Signal)
        {
            return dispatch_semaphore_wait(Signal, dispatch_time(DISPATCH_TIME_NOW, KLoadTimeoutNs)) == 0;
        }

        std::string Describe(NSError* Error, const char* Fallback)
        {
            return Error ? std::string(Error.localizedDescription.UTF8String) : std::string(Fallback);
        }
    } // namespace

    VideoDecoder::VideoDecoder(std::unique_ptr<Impl> Value) : Impl_(std::move(Value))
    {
    }

    VideoDecoder::~VideoDecoder()
    {
        [[NSNotificationCenter defaultCenter] removeObserver:Impl_->EndObserver];
        [Impl_->Player pause];
        [Impl_->Player replaceCurrentItemWithPlayerItem:nil];
    }

    std::unique_ptr<VideoDecoder> VideoDecoder::Open(const std::string& Path, std::string& Reason)
    {
        @autoreleasepool
        {
            NSString* FilePath = [NSString stringWithUTF8String:Path.c_str()];
            if (!FilePath)
            {
                Reason = "the path is not valid UTF-8";
                return nullptr;
            }
            AVURLAsset* Asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:FilePath] options:nil];
            dispatch_semaphore_t Loaded = dispatch_semaphore_create(0);
            __block NSArray<AVAssetTrack*>* Tracks = nil;
            __block NSError* LoadError = nil;
            [Asset loadTracksWithMediaType:AVMediaTypeVideo
                         completionHandler:^(NSArray<AVAssetTrack*>* Result, NSError* Failure) {
                           Tracks = Result;
                           LoadError = Failure;
                           dispatch_semaphore_signal(Loaded);
                         }];
            if (!Await(Loaded))
            {
                Reason = "AVFoundation did not open the file in time";
                return nullptr;
            }
            if (Tracks.count == 0)
            {
                Reason = Describe(LoadError, "AVFoundation found no video track");
                return nullptr;
            }

            AVAssetTrack* Track = Tracks.firstObject;
            [Track loadValuesAsynchronouslyForKeys:@[ @"naturalSize", @"decodable" ]
                                 completionHandler:^{
                                   dispatch_semaphore_signal(Loaded);
                                 }];
            if (!Await(Loaded))
            {
                Reason = "AVFoundation did not read the video track in time";
                return nullptr;
            }
            NSError* KeyError = nil;
            if ([Track statusOfValueForKey:@"decodable" error:&KeyError] != AVKeyValueStatusLoaded ||
                [Track statusOfValueForKey:@"naturalSize" error:&KeyError] != AVKeyValueStatusLoaded)
            {
                Reason = Describe(KeyError, "AVFoundation could not read the video track");
                return nullptr;
            }
            // Legacy AVI codecs (Indeo, Cinepak, MS Video 1, RLE) demux but do not decode.
            if (!Track.decodable)
            {
                Reason = "AVFoundation cannot decode its video codec";
                return nullptr;
            }

            auto Value = std::make_unique<Impl>();
            Value->Width = static_cast<int>(std::lround(Track.naturalSize.width));
            Value->Height = static_cast<int>(std::lround(Track.naturalSize.height));
            Value->Item = [AVPlayerItem playerItemWithAsset:Asset];
            NSDictionary* Attributes = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
            Value->Output = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:Attributes];
            [Value->Item addOutput:Value->Output];
            Value->Player = [AVPlayer playerWithPlayerItem:Value->Item];
            Value->Player.actionAtItemEnd = AVPlayerActionAtItemEndPause;
            std::shared_ptr<std::atomic<bool>> Ended = Value->Ended;
            Value->EndObserver =
                [[NSNotificationCenter defaultCenter] addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                                                                  object:Value->Item
                                                                   queue:nil
                                                              usingBlock:^(NSNotification*) {
                                                                Ended->store(true);
                                                              }];
            return std::unique_ptr<VideoDecoder>(new VideoDecoder(std::move(Value)));
        }
    }

    int VideoDecoder::Width() const
    {
        return Impl_->Width;
    }

    int VideoDecoder::Height() const
    {
        return Impl_->Height;
    }

    bool VideoDecoder::Start()
    {
        Impl_->Ended->store(false);
        [Impl_->Player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        [Impl_->Player play];
        return true;
    }

    void VideoDecoder::Pause()
    {
        [Impl_->Player pause];
    }

    void VideoDecoder::Resume()
    {
        [Impl_->Player play];
    }

    bool VideoDecoder::Finished() const
    {
        return Impl_->Ended->load();
    }

    VideoFrame VideoDecoder::CurrentFrame()
    {
        @autoreleasepool
        {
            const CMTime Time = Impl_->Item.currentTime;
            if ([Impl_->Output hasNewPixelBufferForItemTime:Time])
            {
                CVPixelBufferRef Buffer = [Impl_->Output copyPixelBufferForItemTime:Time itemTimeForDisplay:nullptr];
                if (Buffer && CVPixelBufferLockBaseAddress(Buffer, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess)
                {
                    const auto Width = static_cast<int>(CVPixelBufferGetWidth(Buffer));
                    const auto Height = static_cast<int>(CVPixelBufferGetHeight(Buffer));
                    const std::size_t SourcePitch = CVPixelBufferGetBytesPerRow(Buffer);
                    const std::size_t RowBytes = static_cast<std::size_t>(Width) * 4;
                    const auto* Source = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(Buffer));
                    Impl_->Pixels.resize(RowBytes * static_cast<std::size_t>(Height));
                    for (int Row = 0; Row < Height; ++Row)
                        std::memcpy(Impl_->Pixels.data() + RowBytes * static_cast<std::size_t>(Row),
                                    Source + SourcePitch * static_cast<std::size_t>(Row), RowBytes);
                    Impl_->FrameWidth = Width;
                    Impl_->FrameHeight = Height;
                    CVPixelBufferUnlockBaseAddress(Buffer, kCVPixelBufferLock_ReadOnly);
                }
                if (Buffer)
                    CVPixelBufferRelease(Buffer);
            }
        }
        if (Impl_->Pixels.empty())
            return {};
        return {Impl_->Pixels.data(), Impl_->FrameWidth, Impl_->FrameHeight, Impl_->FrameWidth * 4};
    }

} // namespace dbc
