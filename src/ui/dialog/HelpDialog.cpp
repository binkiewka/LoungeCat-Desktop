#include "ui/dialog/HelpDialog.h"
#include <QVBoxLayout>
#include <QTextBrowser>
#include <QDialogButtonBox>

namespace loungecat {

HelpDialog::HelpDialog(const std::optional<QString>& initialFilter, QWidget* parent)
    : QDialog(parent) {
    Q_UNUSED(initialFilter)
    setWindowTitle(QStringLiteral("LoungeCat IRC Commands & Keyboard Shortcuts"));
    resize(620, 520);

    auto* layout = new QVBoxLayout(this);
    auto* browser = new QTextBrowser(this);
    browser->setHtml(QStringLiteral(R"(
        <h2>LoungeCat IRC Commands Reference</h2>
        <h3>Channel &amp; Connection</h3>
        <ul>
            <li><b>/join &lt;#channel&gt; [key]</b> (/j) — Join a channel</li>
            <li><b>/part [#channel] [reason]</b> (/leave, /p) — Leave a channel</li>
            <li><b>/close [target]</b> (/wc) — Close active channel or private query</li>
            <li><b>/cycle</b> (/rejoin) — Part and immediately rejoin active channel</li>
            <li><b>/topic [newTopic]</b> (/t) — View or change channel topic</li>
            <li><b>/list [filter]</b> — Browse channels on the server</li>
            <li><b>/names [#channel]</b> — Refresh channel user list</li>
            <li><b>/connect &lt;host&gt; [port]</b> (/server) — Connect to an IRC server</li>
            <li><b>/reconnect</b> — Reconnect to current server</li>
            <li><b>/quit [reason]</b> (/disconnect) — Disconnect from server</li>
        </ul>
        <h3>Messaging &amp; Users</h3>
        <ul>
            <li><b>/msg &lt;target&gt; &lt;text&gt;</b> (/m, /q) — Send private or channel message</li>
            <li><b>/query &lt;nick&gt; [text]</b> (/pm) — Open private message tab</li>
            <li><b>/me &lt;action&gt;</b> (/action) — Send CTCP action</li>
            <li><b>/notice &lt;target&gt; &lt;text&gt;</b> (/n) — Send IRC notice</li>
            <li><b>/nick &lt;newNick&gt;</b> — Change your nickname</li>
            <li><b>/whois &lt;nick&gt;</b> (/wi, /wii) — Query user WHOIS details</li>
            <li><b>/ignore &lt;nick&gt;</b> / <b>/unignore &lt;nick&gt;</b> / <b>/ignorelist</b> — Manage ignored users</li>
            <li><b>/away [msg]</b> / <b>/back</b> — Set or clear away status</li>
        </ul>
        <h3>Channel Moderation</h3>
        <ul>
            <li><b>/op &lt;nick&gt;</b> / <b>/deop &lt;nick&gt;</b> — Grant or revoke Operator (+o)</li>
            <li><b>/voice &lt;nick&gt;</b> / <b>/devoice &lt;nick&gt;</b> — Grant or revoke Voice (+v)</li>
            <li><b>/kick &lt;nick&gt; [reason]</b> (/k) — Kick user from channel</li>
            <li><b>/ban &lt;mask&gt;</b> (/b) / <b>/unban &lt;mask&gt;</b> — Ban or unban hostmask</li>
            <li><b>/kickban &lt;nick&gt; [reason]</b> (/kb) — Ban and kick user</li>
            <li><b>/quiet &lt;mask&gt;</b> (/mute) / <b>/unquiet &lt;mask&gt;</b> — Quiet or unquiet user</li>
        </ul>
        <h3>Services &amp; Fun</h3>
        <ul>
            <li><b>/ns</b>, <b>/cs</b>, <b>/ms</b>, <b>/os</b>, <b>/hs</b>, <b>/bs</b> — Message NickServ, ChanServ, etc.</li>
            <li><b>/sysinfo</b> — Send system hardware/OS summary to channel</li>
            <li><b>/uploadimage [file]</b> (/img) — Upload image from file or clipboard via ImgBB</li>
            <li><b>/slap &lt;nick&gt;</b>, <b>/shrug</b>, <b>/tableflip</b>, <b>/unflip</b>, <b>/lenny</b> — Fun emotes</li>
            <li><b>/clear</b> (/cls), <b>/clearall</b> — Clear chat history</li>
        </ul>
        <h3>Keyboard Shortcuts</h3>
        <ul>
            <li><b>Ctrl+F</b> — Search in active channel</li>
            <li><b>Ctrl+M</b> — Auto-fix all spell-check typos in input box</li>
            <li><b>Ctrl+B / Ctrl+I / Ctrl+U</b> — Insert Bold / Italic / Underline control codes</li>
            <li><b>Tab / Shift+Tab</b> — Cycle nickname, channel, or command completion</li>
            <li><b>Up / Down</b> — Cycle sent input history</li>
        </ul>
    )"));
    layout->addWidget(browser, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btnBox);
}

} // namespace loungecat
