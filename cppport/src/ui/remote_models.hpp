#pragma once

// The view models the phone's tool pages use (features/RemoteMode's Tools and
// Config routes): bound into the RemoteService under a name each, with the
// methods and properties a pendant may use. The models are the desktop's own,
// so a button on the phone does what the same button does on the screen.

class QObject;

namespace gs::app {
class RemoteService;
}

namespace gs::ui {

// `owner` parents the models (made when a pendant first asks for one).
void bindRemoteModels(app::RemoteService& remote, QObject* owner);

}  // namespace gs::ui
