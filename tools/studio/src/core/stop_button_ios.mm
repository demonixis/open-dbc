#include "stop_button.hpp"

#include <SDL3/SDL.h>

#import <UIKit/UIKit.h>

namespace
{
    // Set by the button and read by the Studio's loop, both on the main thread.
    bool StopRequested = false;
    UIButton* StopButton = nil;
} // namespace

@interface OpenDbcStopTarget : NSObject
- (void)pressed;
@end

@implementation OpenDbcStopTarget
- (void)pressed
{
    StopRequested = true;
}
@end

namespace dbc::studio
{
    namespace core
    {
        void ShowStopButton(SDL_Window* Window)
        {
            HideStopButton();
            StopRequested = false;
            UIWindow* Native = (__bridge UIWindow*)SDL_GetPointerProperty(
                SDL_GetWindowProperties(Window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
            // The window itself, above the view the program draws in.
            UIView* Host = Native;
            if (Host == nil)
                return;

            static OpenDbcStopTarget* Target = [[OpenDbcStopTarget alloc] init];
            UIImageSymbolConfiguration* Size = [UIImageSymbolConfiguration configurationWithPointSize:30.0];
            UIButton* Button = [UIButton buttonWithType:UIButtonTypeSystem];
            [Button setImage:[UIImage systemImageNamed:@"xmark.circle.fill" withConfiguration:Size]
                    forState:UIControlStateNormal];
            Button.tintColor = UIColor.whiteColor;
            // Seen over a bright scene as over a dark one, without hiding much of either.
            Button.alpha = 0.7;
            Button.layer.shadowColor = UIColor.blackColor.CGColor;
            Button.layer.shadowOpacity = 0.8f;
            Button.layer.shadowRadius = 3.0;
            Button.layer.shadowOffset = CGSizeZero;
            Button.accessibilityLabel = @"Stop";
            Button.translatesAutoresizingMaskIntoConstraints = NO;
            [Button addTarget:Target action:@selector(pressed) forControlEvents:UIControlEventTouchUpInside];
            [Host addSubview:Button];
            UILayoutGuide* Safe = Host.safeAreaLayoutGuide;
            [NSLayoutConstraint activateConstraints:@[
                [Button.topAnchor constraintEqualToAnchor:Safe.topAnchor constant:4.0],
                [Button.trailingAnchor constraintEqualToAnchor:Safe.trailingAnchor constant:-8.0],
                [Button.widthAnchor constraintEqualToConstant:44.0],
                [Button.heightAnchor constraintEqualToConstant:44.0],
            ]];
            StopButton = Button;
        }

        void HideStopButton()
        {
            [StopButton removeFromSuperview];
            StopButton = nil;
        }

        bool TakeStopRequest()
        {
            const bool Requested = StopRequested;
            StopRequested = false;
            return Requested;
        }
    } // namespace core

} // namespace dbc::studio
