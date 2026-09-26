#include <vita3k_ios/TextInput.h>
#import <UIKit/UIKit.h>
#include <mutex>
#include <utility>

namespace {
std::mutex result_mutex;
std::optional<Vita3KIOSTextResult> pending_result;
}

@interface TsubomiTextInputController : UIViewController <UITextViewDelegate>
@property(nonatomic) uint64_t requestID;
@property(nonatomic) NSUInteger maximum;
@property(nonatomic) BOOL multiline;
@property(nonatomic) BOOL cancelable;
@property(nonatomic, copy) NSString *initialText;
@property(nonatomic, strong) UITextView *editor;
@end

@implementation TsubomiTextInputController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.modalInPresentation = YES;
    self.editor = [[UITextView alloc] init];
    self.editor.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    self.editor.text = self.initialText;
    self.editor.delegate = self;
    self.editor.autocorrectionType = UITextAutocorrectionTypeNo;
    self.editor.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.editor];
    [NSLayoutConstraint activateConstraints:@[
        [self.editor.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:16],
        [self.editor.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-16],
        [self.editor.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:12],
        [self.editor.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor constant:-12],
    ]];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(submit)];
    if (self.cancelable)
        self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel target:self action:@selector(cancel)];
    [self textViewDidChange:self.editor];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self.editor becomeFirstResponder];
}
- (void)textViewDidChange:(UITextView *)textView {
    self.navigationItem.prompt = [NSString stringWithFormat:@"%lu / %lu", (unsigned long)textView.text.length, (unsigned long)self.maximum];
    self.navigationItem.rightBarButtonItem.enabled = textView.text.length <= self.maximum && !textView.markedTextRange;
}
- (void)textViewDidChangeSelection:(UITextView *)textView {
    [self textViewDidChange:textView];
}
- (BOOL)textView:(UITextView *)textView shouldChangeTextInRange:(NSRange)range replacementText:(NSString *)text {
    (void)range;
    if (!self.multiline && !textView.markedTextRange && [text isEqualToString:@"\n"]) {
        if (self.navigationItem.rightBarButtonItem.enabled)
            [self submit];
        return NO;
    }
    if (!self.multiline && [text rangeOfCharacterFromSet:NSCharacterSet.newlineCharacterSet].location != NSNotFound)
        return NO;
    return YES; // Let composed input finish before enforcing the UTF-16 limit.
}
- (void)finish:(BOOL)cancelled {
    NSString *value = self.editor.text ?: @"";
    std::u16string text(value.length, u'\0');
    [value getCharacters:reinterpret_cast<unichar *>(text.data()) range:NSMakeRange(0, value.length)];
    vita3k_ios_limit_text(text, self.maximum);
    {
        const std::lock_guard lock(result_mutex);
        pending_result = Vita3KIOSTextResult{self.requestID, std::move(text), static_cast<bool>(cancelled)};
    }
    [self.editor resignFirstResponder];
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)submit { [self finish:NO]; }
- (void)cancel { [self finish:YES]; }
@end

static UINavigationController *text_controller;
static uint64_t shown_id = 0;

void vita3k_ios_update_text_input(const std::optional<Vita3KIOSTextRequest> &request) {
    if (!NSThread.isMainThread) {
        const auto copy = request;
        dispatch_async(dispatch_get_main_queue(), ^{ vita3k_ios_update_text_input(copy); });
        return;
    }
    if (!request || (shown_id && shown_id != request->id)) {
        [text_controller dismissViewControllerAnimated:NO completion:nil];
        text_controller = nil;
        shown_id = 0;
    }
    if (!request || shown_id == request->id)
        return;
    UIViewController *root = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState != UISceneActivationStateForegroundActive)
            continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows)
            if (window.isKeyWindow)
                root = window.rootViewController;
    }
    if (!root || root.presentedViewController)
        return; // Retry after another sheet (or app activation) finishes.
    TsubomiTextInputController *editor = [[TsubomiTextInputController alloc] init];
    editor.requestID = request->id;
    editor.title = [NSString stringWithUTF8String:request->title.c_str()] ?: @"Enter text";
    editor.initialText = [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar *>(request->text.data()) length:request->text.size()];
    editor.maximum = request->maximum;
    editor.multiline = request->multiline;
    editor.cancelable = request->cancelable;
    text_controller = [[UINavigationController alloc] initWithRootViewController:editor];
    text_controller.modalInPresentation = YES;
    shown_id = request->id;
    [root presentViewController:text_controller animated:YES completion:nil];
}

std::optional<Vita3KIOSTextResult> vita3k_ios_take_text_result() {
    const std::lock_guard lock(result_mutex);
    auto result = std::move(pending_result);
    pending_result.reset();
    return result;
}
