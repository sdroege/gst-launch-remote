/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#import "ViewController.h"

#include "gst-launch-remote.h"

@interface ViewController () {
    GstLaunchRemote *launch;
    int media_width;
    int media_height;
    Boolean dragging_slider;
}

@end

@implementation ViewController

- (void) updateTimeWidget
{
    NSInteger position = time_slider.value / 1000;
    NSInteger duration = time_slider.maximumValue / 1000;
    NSString *position_txt = @" -- ";
    NSString *duration_txt = @" -- ";
    
    if (duration > 0) {
        NSUInteger hours = duration / (60 * 60);
        NSUInteger minutes = (duration / 60) % 60;
        NSUInteger seconds = duration % 60;
        
        duration_txt = [NSString stringWithFormat:@"%02u:%02u:%02u", hours, minutes, seconds];
    }
    if (position > 0) {
        NSUInteger hours = position / (60 * 60);
        NSUInteger minutes = (position / 60) % 60;
        NSUInteger seconds = position % 60;
        
        position_txt = [NSString stringWithFormat:@"%02u:%02u:%02u", hours, minutes, seconds];
    }
    
    NSString *text = [NSString stringWithFormat:@"%@ / %@",
                      position_txt, duration_txt];
    
    time_label.text = text;
}

static void set_message_proxy (const gchar *message, gpointer app)
{
    ViewController *self = (__bridge ViewController *) app;
    [self setMessage:[NSString stringWithUTF8String:message]];
}

void set_current_position_proxy (gint position, gint duration, gpointer app)
{
    ViewController *self = (__bridge ViewController *) app;
    [self setCurrentPosition:position duration:duration];
}

void initialized_proxy (gpointer app)
{
    ViewController *self = (__bridge ViewController *) app;
    [self initialized];
}

void media_size_changed_proxy (gint width, gint height, gpointer app)
{
    ViewController *self = (__bridge ViewController *) app;
    [self mediaSizeChanged:width height:height];
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    [UIApplication sharedApplication].idleTimerDisabled = YES;
    
    play_button.enabled = FALSE;
    pause_button.enabled = FALSE;
    
    media_width = 320;
    media_height = 240;
    
    GstLaunchRemoteAppContext ctx;
    ctx.app = (__bridge gpointer)(self);
    ctx.initialized = initialized_proxy;
    ctx.media_size_changed = media_size_changed_proxy;
    ctx.set_current_position = set_current_position_proxy;
    ctx.set_message = set_message_proxy;
    
    launch = gst_launch_remote_new(&ctx);
    
    gst_launch_remote_set_window_handle(launch, (guintptr) (id) video_view);
}

- (void)viewDidDisappear:(BOOL)animated
{
    if (launch)
    {
        gst_launch_remote_free(launch);
    }
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

-(IBAction) play:(id)sender
{
    gst_launch_remote_play(launch);
}

-(IBAction) pause:(id)sender
{
    gst_launch_remote_pause(launch);
}

- (IBAction)sliderValueChanged:(id)sender {
    if (!dragging_slider) return;
    [self updateTimeWidget];
}

- (IBAction)sliderTouchDown:(id)sender {
    dragging_slider = YES;
}

- (IBAction)sliderTouchUp:(id)sender {
    dragging_slider = NO;
    gst_launch_remote_seek (launch, time_slider.value);
}

- (void)viewDidLayoutSubviews
{
    CGFloat view_width = video_container_view.bounds.size.width;
    CGFloat view_height = video_container_view.bounds.size.height;
    
    CGFloat correct_height = view_width * media_height / media_width;
    CGFloat correct_width = view_height * media_width / media_height;
    
    if (correct_height < view_height) {
        video_height_constraint.constant = correct_height;
        video_width_constraint.constant = view_width;
    } else {
        video_width_constraint.constant = correct_width;
        video_height_constraint.constant = view_height;
    }
    
    time_slider.frame = CGRectMake(time_slider.frame.origin.x, time_slider.frame.origin.y, toolbar.frame.size.width - time_slider.frame.origin.x - 8, time_slider.frame.size.height);
}

-(void) initialized
{
    dispatch_async(dispatch_get_main_queue(), ^{
        play_button.enabled = TRUE;
        pause_button.enabled = TRUE;
        message_label.text = @"Ready";
    });
}

-(void) setMessage:(NSString *)message
{
    dispatch_async(dispatch_get_main_queue(), ^{
        message_label.text = message;
    });
}

-(void) mediaSizeChanged:(NSInteger)width height:(NSInteger)height
{
    media_width = width;
    media_height = height;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self viewDidLayoutSubviews];
        [video_view setNeedsLayout];
        [video_view layoutIfNeeded];
    });
}

-(void) setCurrentPosition:(NSInteger)position duration:(NSInteger)duration
{
    /* Ignore messages from the pipeline if the time sliders is being dragged */
    if (dragging_slider) return;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        time_slider.maximumValue = duration;
        time_slider.value = position;
        [self updateTimeWidget];
    });
}

@end
