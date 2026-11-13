#include "screen_keyboard.hpp"

#import <UIKit/UIKit.h>

namespace dbc::studio
{
    namespace core
    {
        namespace
        {
            // Written by the notifications below and read by the Studio's frame, both on the main thread.
            float KeyboardHeight = 0.0f;

            void Observe()
            {
                NSNotificationCenter* Center = [NSNotificationCenter defaultCenter];
                [Center addObserverForName:UIKeyboardWillChangeFrameNotification
                                    object:nil
                                     queue:nil
                                usingBlock:^(NSNotification* Note) {
                                  const CGRect End = [Note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
                                  const CGFloat ScreenHeight = UIScreen.mainScreen.bounds.size.height;
                                  // A floating or split keyboard does not reach the bottom and covers no band.
                                  const bool Docked = CGRectGetMaxY(End) >= ScreenHeight - 1.0;
                                  KeyboardHeight =
                                      Docked ? static_cast<float>(MAX(0.0, ScreenHeight - End.origin.y)) : 0.0f;
                                }];
                [Center addObserverForName:UIKeyboardWillHideNotification
                                    object:nil
                                     queue:nil
                                usingBlock:^(NSNotification*) {
                                  KeyboardHeight = 0.0f;
                                }];
            }
        } // namespace

        float ScreenKeyboardHeight()
        {
            static dispatch_once_t Once;
            dispatch_once(&Once, ^{
              Observe();
            });
            return KeyboardHeight;
        }
    } // namespace core

} // namespace dbc::studio
