/*
 *  Copyright (C) 2015-2018 Savoir-faire Linux Inc.
 *  Author: Stepan Salenikovich <stepan.salenikovich@savoirfairelinux.com>
 *  Author: Guillaume Roguez <guillaume.roguez@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#include "ringmainwindow.h"

// GTK+ related
#include <glib/gi18n.h>

// std
#include <algorithm>

// LRC
#include <accountmodel.h> // Old lrc but still used
#include <api/account.h>
#include <api/contact.h>
#include <api/profile.h>
#include <api/contactmodel.h>
#include <api/conversation.h>
#include <api/conversationmodel.h>
#include <api/datatransfermodel.h>
#include <api/lrc.h>
#include <api/newaccountmodel.h>
#include <api/newcallmodel.h>
#include <api/behaviorcontroller.h>
#include "api/account.h"
#include <media/textrecording.h>
#include <media/recordingmodel.h>
#include <media/text.h>

// Ring client
#include "newaccountsettingsview.h"
#include "accountmigrationview.h"
#include "accountcreationwizard.h"
#include "chatview.h"
#include "conversationsview.h"
#include "currentcallview.h"
#include "dialogs.h"
#include "generalsettingsview.h"
#include "incomingcallview.h"
#include "mediasettingsview.h"
#include "models/gtkqtreemodel.h"
#include "ringwelcomeview.h"
#include "utils/accounts.h"
#include "utils/files.h"
#include "ringnotify.h"
#include "accountinfopointer.h"
#include "native/pixbufmanipulator.h"
#include "ringnotify.h"

//==============================================================================

namespace { namespace details
{
class CppImpl;
}}

struct _RingMainWindow
{
    GtkApplicationWindow parent;
};

struct _RingMainWindowClass
{
    GtkApplicationWindowClass parent_class;
};

struct RingMainWindowPrivate
{
    GtkWidget *ring_menu;
    GtkWidget *image_ring;
    GtkWidget *ring_settings;
    GtkWidget *image_settings;
    GtkWidget *hbox_settings;
    GtkWidget *notebook_contacts;
    GtkWidget *scrolled_window_smartview;
    GtkWidget *treeview_conversations;
    GtkWidget *vbox_left_pane;
    GtkWidget *search_entry;
    GtkWidget *stack_main_view;
    GtkWidget *vbox_call_view;
    GtkWidget *frame_call;
    GtkWidget *welcome_view;
    GtkWidget *button_new_conversation;
    GtkWidget *media_settings_view;
    GtkWidget *new_account_settings_view;
    GtkWidget *general_settings_view;
    GtkWidget *last_settings_view;
    GtkWidget *radiobutton_new_account_settings;
    GtkWidget *radiobutton_general_settings;
    GtkWidget *radiobutton_media_settings;
    GtkWidget *account_creation_wizard;
    GtkWidget *account_migration_view;
    GtkWidget *combobox_account_selector;
    GtkWidget *treeview_contact_requests;
    GtkWidget *scrolled_window_contact_requests;
    GtkWidget *webkit_chat_container; ///< The webkit_chat_container is created once, then reused for all chat views

    GtkWidget *notifier;

    GSettings *settings;

    details::CppImpl* cpp; ///< Non-UI and C++ only code

    gulong update_download_folder;
    gulong notif_chat_view;
    gulong notif_accept_pending;
    gulong notif_refuse_pending;
    gulong notif_accept_call;
    gulong notif_decline_call;
    gboolean set_top_account_flag = true;
};

G_DEFINE_TYPE_WITH_PRIVATE(RingMainWindow, ring_main_window, GTK_TYPE_APPLICATION_WINDOW);

#define RING_MAIN_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), RING_MAIN_WINDOW_TYPE, RingMainWindowPrivate))

//==============================================================================

namespace { namespace details
{

static constexpr const char* CALL_VIEW_NAME                    = "calls";
static constexpr const char* ACCOUNT_CREATION_WIZARD_VIEW_NAME = "account-creation-wizard";
static constexpr const char* ACCOUNT_MIGRATION_VIEW_NAME       = "account-migration-view";
static constexpr const char* GENERAL_SETTINGS_VIEW_NAME        = "general";
static constexpr const char* MEDIA_SETTINGS_VIEW_NAME          = "media";
static constexpr const char* NEW_ACCOUNT_SETTINGS_VIEW_NAME    = "account";

inline namespace helpers
{

/**
 * set the column value by printing the alias and the state of an account in combobox_account_selector.
 */
static void
print_account_and_state(G_GNUC_UNUSED GtkCellLayout* cell_layout,
                        GtkCellRenderer* cell,
                        GtkTreeModel* model,
                        GtkTreeIter* iter,
                        G_GNUC_UNUSED gpointer* data)
{
    gchar *id;
    gchar *alias;
    gchar *registeredName;
    gchar *uri;
    gchar *text;

    gtk_tree_model_get (model, iter,
                        0 /* col# */, &id /* data */,
                        3 /* col# */, &uri /* data */,
                        4 /* col# */, &alias /* data */,
                        5 /* col# */, &registeredName /* data */,
                        -1);

    if (g_strcmp0("", id) == 0) {
        text = g_markup_printf_escaped(
            "<span font=\"10\">%s</span>",
            _("Add account…")
        );
    } else if (g_strcmp0("", registeredName) == 0) {
        if (g_strcmp0(uri, alias) == 0) {
            text = g_markup_printf_escaped(
                "<span font=\"10\">%s</span>",
                alias
            );
        } else {
            text = g_markup_printf_escaped(
                "<span font=\"10\">%s</span>\n<span font=\"7\">%s</span>",
                alias, uri
            );
        }
    } else {
        if (g_strcmp0(alias, registeredName) == 0) {
            text = g_markup_printf_escaped(
                "<span font=\"10\">%s</span>",
                alias
            );
        } else {
            text = g_markup_printf_escaped(
                "<span font=\"10\">%s</span>\n<span font=\"7\">%s</span>",
                alias, registeredName
            );
        }
    }

    g_object_set(G_OBJECT(cell), "markup", text, NULL);
    g_object_set(G_OBJECT(cell), "height", 17, NULL);
    g_object_set(G_OBJECT(cell), "ypad", 0, NULL);

    g_free(id);
    g_free(alias);
    g_free(registeredName);
    g_free(uri);
    g_free(text);
}

static void
render_account_avatar(G_GNUC_UNUSED GtkCellLayout* cell_layout,
                      GtkCellRenderer *cell,
                      GtkTreeModel *model,
                      GtkTreeIter *iter,
                      G_GNUC_UNUSED gpointer data)
{
    gchar *id;
    gchar* avatar;
    gchar* status;

    gtk_tree_model_get (model, iter,
                        0 /* col# */, &id /* data */,
                        1 /* col# */, &status /* data */,
                        2 /* col# */, &avatar /* data */,
                        -1);

    if (g_strcmp0("", id) == 0) {
        g_free(status);
        g_free(avatar);
        g_free(id);

        GdkPixbuf* icon = gdk_pixbuf_new_from_resource("/cx/ring/RingGnome/add-device", nullptr);
        g_object_set(G_OBJECT(cell), "width", 32, nullptr);
        g_object_set(G_OBJECT(cell), "height", 32, nullptr);
        g_object_set(G_OBJECT(cell), "pixbuf", icon, nullptr);
        return;
    }

    IconStatus iconStatus = IconStatus::INVALID;
    std::string statusStr = status? status : "";
    if (statusStr == "DISCONNECTED") {
        iconStatus = IconStatus::DISCONNECTED;
    } else if (statusStr == "TRYING") {
        iconStatus = IconStatus::TRYING;
    } else if (statusStr == "CONNECTED") {
        iconStatus = IconStatus::CONNECTED;
    }
    auto default_avatar = Interfaces::PixbufManipulator().generateAvatar("", "");
    auto default_scaled = Interfaces::PixbufManipulator().scaleAndFrame(default_avatar.get(), QSize(32, 32), true, iconStatus);
    auto photo = default_scaled;

    std::string photostr = avatar;
    if (!photostr.empty()) {
        QByteArray byteArray(photostr.c_str(), photostr.length());
        QVariant avatar = Interfaces::PixbufManipulator().personPhoto(byteArray);
        auto pixbuf_photo = Interfaces::PixbufManipulator().scaleAndFrame(avatar.value<std::shared_ptr<GdkPixbuf>>().get(), QSize(32, 32), true, iconStatus);
        if (avatar.isValid()) {
            photo = pixbuf_photo;
        }
    }

    g_object_set(G_OBJECT(cell), "width", 32, nullptr);
    g_object_set(G_OBJECT(cell), "height", 32, nullptr);
    g_object_set(G_OBJECT(cell), "pixbuf", photo.get(), nullptr);

    g_free(status);
    g_free(avatar);
    g_free(id);
}

inline static void
foreachLrcAccount(const lrc::api::Lrc& lrc,
                  const std::function<void(const lrc::api::account::Info&)>& func)
{
    auto& account_model = lrc.getAccountModel();
    for (const auto& account_id : account_model.getAccountList()) {
        const auto& account_info = account_model.getAccountInfo(account_id);
            func(account_info);
    }
}

} // namespace helpers

class CppImpl
{
public:
    explicit CppImpl(RingMainWindow& widget);
    ~CppImpl();

    void init();
    void updateLrc(const std::string& accountId, const std::string& accountIdToFlagFreeable = "");
    void changeView(GType type, lrc::api::conversation::Info conversation = {});
    void enterFullScreen();
    void leaveFullScreen();
    void toggleFullScreen();
    void resetToWelcome();
    void refreshPendingContactRequestTab();
    void changeAccountSelection(const std::string& id);
    void onAccountSelectionChange(const std::string& id);
    void enterAccountCreationWizard(bool showControls = false);
    void leaveAccountCreationWizard();
    void enterSettingsView();
    void leaveSettingsView();

    lrc::api::conversation::Info getCurrentConversation(GtkWidget* frame_call);

    void showAccountSelectorWidget(bool show = true);
    std::size_t refreshAccountSelectorWidget(int selection_row = -1, const std::string& selected = "");

    WebKitChatContainer* webkitChatContainer() const;

    RingMainWindow* self = nullptr; // The GTK widget itself
    RingMainWindowPrivate* widgets = nullptr;

    std::unique_ptr<lrc::api::Lrc> lrc_;
    AccountInfoPointer accountInfo_ = nullptr;
    AccountInfoPointer accountInfoForMigration_ = nullptr;
    std::unique_ptr<lrc::api::conversation::Info> chatViewConversation_;
    lrc::api::profile::Type currentTypeFilter_;
    bool show_settings = false;
    bool is_fullscreen = false;
    bool has_cleared_all_history = false;

    int smartviewPageNum = 0;
    int contactRequestsPageNum = 0;

    QMetaObject::Connection showChatViewConnection_;
    QMetaObject::Connection showLeaveMessageViewConnection_;
    QMetaObject::Connection showCallViewConnection_;
    QMetaObject::Connection showIncomingViewConnection_;
    QMetaObject::Connection newTrustRequestNotification_;
    QMetaObject::Connection closeTrustRequestNotification_;
    QMetaObject::Connection slotNewInteraction_;
    QMetaObject::Connection slotReadInteraction_;
    QMetaObject::Connection changeAccountConnection_;
    QMetaObject::Connection newAccountConnection_;
    QMetaObject::Connection rmAccountConnection_;
    QMetaObject::Connection invalidAccountConnection_;
    QMetaObject::Connection historyClearedConnection_;
    QMetaObject::Connection modelSortedConnection_;
    QMetaObject::Connection callChangedConnection_;
    QMetaObject::Connection newIncomingCallConnection_;
    QMetaObject::Connection filterChangedConnection_;
    QMetaObject::Connection newConversationConnection_;
    QMetaObject::Connection conversationRemovedConnection_;
    QMetaObject::Connection accountStatusChangedConnection_;
    QMetaObject::Connection profileUpdatedConnection_;

private:
    CppImpl() = delete;
    CppImpl(const CppImpl&) = delete;
    CppImpl& operator=(const CppImpl&) = delete;

    GtkWidget* displayWelcomeView(lrc::api::conversation::Info);
    GtkWidget* displayIncomingView(lrc::api::conversation::Info);
    GtkWidget* displayCurrentCallView(lrc::api::conversation::Info);
    GtkWidget* displayChatView(lrc::api::conversation::Info);

    // Callbacks used as LRC Qt slot
    void slotAccountAddedFromLrc(const std::string& id);
    void slotAccountRemovedFromLrc(const std::string& id);
    void slotInvalidAccountFromLrc(const std::string& id);
    void slotAccountStatusChanged(const std::string& id);
    void slotConversationCleared(const std::string& uid);
    void slotModelSorted();
    void slotNewIncomingCall(const std::string& callId);
    void slotCallStatusChanged(const std::string& callId);
    void slotFilterChanged();
    void slotNewConversation(const std::string& uid);
    void slotConversationRemoved(const std::string& uid);
    void slotShowChatView(const std::string& id, lrc::api::conversation::Info origin);
    void slotShowLeaveMessageView(lrc::api::conversation::Info conv);
    void slotShowCallView(const std::string& id, lrc::api::conversation::Info origin);
    void slotShowIncomingCallView(const std::string& id, lrc::api::conversation::Info origin);
    void slotNewTrustRequest(const std::string& id, const std::string& contactUri);
    void slotCloseTrustRequest(const std::string& id, const std::string& contactUri);
    void slotNewInteraction(const std::string& accountId, const std::string& conversation,
                                uint64_t, const lrc::api::interaction::Info& interaction);
    void slotCloseInteraction(const std::string& accountId, const std::string& conversation, uint64_t);
    void slotProfileUpdated(const std::string& id);
};

inline namespace gtk_callbacks
{

static void
on_video_double_clicked(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    priv->cpp->toggleFullScreen();
}

static void
on_hide_view_clicked(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    priv->cpp->resetToWelcome();
}

static void
on_account_creation_completed(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    priv->cpp->leaveAccountCreationWizard();
}

static void
on_account_changed(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    auto changeTopAccount = priv->set_top_account_flag;
    if (!priv->set_top_account_flag) {
        priv->set_top_account_flag = true;
    }

    auto accountComboBox = GTK_COMBO_BOX(priv->combobox_account_selector);
    auto model = gtk_combo_box_get_model(accountComboBox);

    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter(accountComboBox, &iter)) {
        gchar* accountId;
        gtk_tree_model_get(model, &iter, 0 /* col# */, &accountId /* data */, -1);
        if (g_strcmp0("", accountId) == 0) {
            priv->cpp->enterAccountCreationWizard(true);
        } else {
            priv->cpp->leaveAccountCreationWizard();
            if (priv->cpp->accountInfo_ && changeTopAccount) {
                priv->cpp->accountInfo_->accountModel->setTopAccount(accountId);
            }
            priv->cpp->onAccountSelectionChange(accountId);
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(priv->notebook_contacts),
                priv->cpp->accountInfo_->contactModel->hasPendingRequests());
        }
        g_free(accountId);
    }
}

static void
on_settings_clicked(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    if (!priv->cpp->show_settings)
        priv->cpp->enterSettingsView();
    else
        priv->cpp->leaveSettingsView();
}

static void
on_show_media_settings(GtkToggleButton* navbutton, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    if (gtk_toggle_button_get_active(navbutton)) {
        media_settings_view_show_preview(MEDIA_SETTINGS_VIEW(priv->media_settings_view), TRUE);
        gtk_stack_set_visible_child_name(GTK_STACK(priv->stack_main_view), MEDIA_SETTINGS_VIEW_NAME);
        priv->last_settings_view = priv->media_settings_view;
    } else {
        media_settings_view_show_preview(MEDIA_SETTINGS_VIEW(priv->media_settings_view), FALSE);
    }
}


static void
on_show_new_account_settings(GtkToggleButton* navbutton, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    if (gtk_toggle_button_get_active(navbutton)) {
        new_account_settings_view_show(NEW_ACCOUNT_SETTINGS_VIEW(priv->new_account_settings_view), TRUE);
        gtk_stack_set_visible_child_name(GTK_STACK(priv->stack_main_view), NEW_ACCOUNT_SETTINGS_VIEW_NAME);
        priv->last_settings_view = priv->new_account_settings_view;
    } else {
        new_account_settings_view_show(NEW_ACCOUNT_SETTINGS_VIEW(priv->new_account_settings_view), FALSE);
    }
}

static void
on_show_general_settings(GtkToggleButton* navbutton, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    if (gtk_toggle_button_get_active(navbutton)) {
        gtk_stack_set_visible_child_name(GTK_STACK(priv->stack_main_view), GENERAL_SETTINGS_VIEW_NAME);
        priv->last_settings_view = priv->general_settings_view;
    }
}

static void
on_search_entry_text_changed(GtkSearchEntry* search_entry, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    // Filter model
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(search_entry));
    priv->cpp->accountInfo_->conversationModel->setFilter(text);
}

static void
on_search_entry_activated(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    // Select the first conversation of the list
    auto& conversationModel = priv->cpp->accountInfo_->conversationModel;
    auto conversations = conversationModel->allFilteredConversations();

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(priv->search_entry));

    if (!conversations.empty() && text && !std::string(text).empty())
        conversationModel->selectConversation(conversations[0].uid);
}

static gboolean
on_search_entry_key_released(G_GNUC_UNUSED GtkEntry* search_entry, GdkEventKey* key, RingMainWindow* self)
{
    g_return_val_if_fail(IS_RING_MAIN_WINDOW(self), GDK_EVENT_PROPAGATE);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    // if esc key pressed, clear the regex (keep the text, the user might not want to actually delete it)
    if (key->keyval == GDK_KEY_Escape) {
        priv->cpp->accountInfo_->conversationModel->setFilter("");
        return GDK_EVENT_STOP;
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_dtmf_pressed(RingMainWindow* self, GdkEventKey* event, gpointer user_data)
{
    (void)user_data;

    g_return_val_if_fail(IS_RING_MAIN_WINDOW(self), GDK_EVENT_PROPAGATE);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    g_return_val_if_fail(event->type == GDK_KEY_PRESS, GDK_EVENT_PROPAGATE);
    g_return_val_if_fail(priv, GDK_EVENT_PROPAGATE);

    /* we want to react to digit key presses, as long as a GtkEntry is not the
     * input focus
     */
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(self));
    if (GTK_IS_ENTRY(focus))
        return GDK_EVENT_PROPAGATE;

    if (priv->cpp->accountInfo_ &&
        priv->cpp->accountInfo_->profileInfo.type != lrc::api::profile::Type::SIP)
        return GDK_EVENT_PROPAGATE;

    /* filter out cretain MOD masked key presses so that, for example, 'Ctrl+c'
     * does not result in a 'c' being played.
     * we filter Ctrl, Alt, and SUPER/HYPER/META keys */
    if ( event->state
       & ( GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK | GDK_META_MASK ))
       return GDK_EVENT_PROPAGATE;

    // Get current conversation
    auto current_view = gtk_bin_get_child(GTK_BIN(priv->frame_call));
    lrc::api::conversation::Info current_item;
    if (IS_CURRENT_CALL_VIEW(current_view))
       current_item = current_call_view_get_conversation(CURRENT_CALL_VIEW(current_view));
    else
       return GDK_EVENT_PROPAGATE;

    if (current_item.callId.empty())
       return GDK_EVENT_PROPAGATE;

    // pass the character that was entered to be played by the daemon;
    // the daemon will filter out invalid DTMF characters
    guint32 unicode_val = gdk_keyval_to_unicode(event->keyval);
    QString val = QString::fromUcs4(&unicode_val, 1);
    g_debug("attempting to play DTMF tone during ongoing call: %s", val.toUtf8().constData());
    priv->cpp->accountInfo_->callModel->playDTMF(current_item.callId, val.toStdString());
    // always propagate the key, so we don't steal accelerators/shortcuts
    return GDK_EVENT_PROPAGATE;
}

static void
on_tab_changed(GtkNotebook* notebook, GtkWidget* page, guint page_num, RingMainWindow* self)
{
    (void)notebook;
    (void)page;

    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    auto newType = page_num == 0 ? priv->cpp->accountInfo_->profileInfo.type : lrc::api::profile::Type::PENDING;
    if (priv->cpp->currentTypeFilter_ != newType) {
        priv->cpp->currentTypeFilter_ = newType;
        priv->cpp->accountInfo_->conversationModel->setFilter(priv->cpp->currentTypeFilter_);
    }
}

static gboolean
on_window_size_changed(GtkWidget* self, GdkEventConfigure* event, gpointer user_data)
{
    (void)user_data;

    g_return_val_if_fail(IS_RING_MAIN_WINDOW(self), GDK_EVENT_PROPAGATE);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    g_settings_set_int(priv->settings, "window-width", event->width);
    g_settings_set_int(priv->settings, "window-height", event->height);

    return GDK_EVENT_PROPAGATE;
}

static void
on_search_entry_places_call_changed(GSettings* settings, const gchar* key, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    if (g_settings_get_boolean(settings, key)) {
        gtk_widget_set_tooltip_text(priv->button_new_conversation,
                                    C_("button next to search entry will place a new call",
                                       "place call"));
    } else {
        gtk_widget_set_tooltip_text(priv->button_new_conversation,
                                    C_("button next to search entry will open chat", "open chat"));
    }
}

static void
on_handle_account_migrations(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));

    /* If there is an existing migration view, remove it */
    if (priv->account_migration_view) {
        gtk_widget_destroy(priv->account_migration_view);
        priv->account_migration_view = nullptr;
    }

    auto accounts = priv->cpp->lrc_->getAccountModel().getAccountList();
    for (const auto& accountId : accounts) {
        priv->cpp->accountInfoForMigration_ = &priv->cpp->lrc_->getAccountModel().getAccountInfo(accountId);
        if (priv->cpp->accountInfoForMigration_->status == lrc::api::account::Status::ERROR_NEED_MIGRATION) {
            priv->account_migration_view = account_migration_view_new(priv->cpp->accountInfoForMigration_);
            g_signal_connect_swapped(priv->account_migration_view, "account-migration-completed",
                                     G_CALLBACK(on_handle_account_migrations), self);
            g_signal_connect_swapped(priv->account_migration_view, "account-migration-failed",
                                     G_CALLBACK(on_handle_account_migrations), self);

            gtk_widget_hide(priv->ring_settings);
            priv->cpp->showAccountSelectorWidget(false);
            gtk_widget_show(priv->account_migration_view);
            gtk_stack_add_named(
                GTK_STACK(priv->stack_main_view),
                priv->account_migration_view,
                ACCOUNT_MIGRATION_VIEW_NAME);
            gtk_stack_set_visible_child_name(
                GTK_STACK(priv->stack_main_view),
                ACCOUNT_MIGRATION_VIEW_NAME);
            return;
        }
    }

    priv->cpp->accountInfoForMigration_ = nullptr;
    for (const auto& accountId : accounts) {
        auto* accountInfo = &priv->cpp->lrc_->getAccountModel().getAccountInfo(accountId);
        if (accountInfo->enabled) {
            priv->cpp->updateLrc(accountId);
        }
    }
    gtk_widget_show(priv->ring_settings);
    priv->cpp->showAccountSelectorWidget();
}

enum class Action {
    SELECT,
    ACCEPT,
    REFUSE
};

static void
action_notification(gchar* title, RingMainWindow* self, Action action)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self) && title);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    if (!priv->cpp->accountInfo_) {
        g_warning("Notification clicked but accountInfo_ currently empty");
        return;
    }

    std::string titleStr = title;
    auto firstMarker = titleStr.find(":");
    if (firstMarker == std::string::npos) return;
    auto secondMarker = titleStr.find(":", firstMarker + 1);
    if (secondMarker == std::string::npos) return;

    auto id = titleStr.substr(0, firstMarker);
    auto type = titleStr.substr(firstMarker + 1, secondMarker - firstMarker - 1);
    auto information = titleStr.substr(secondMarker + 1);

    if (action == Action::SELECT) {
        // Select conversation
        if (priv->cpp->show_settings) {
            priv->cpp->leaveSettingsView();
        }

        if (priv->cpp->accountInfo_->id != id) {
            priv->cpp->updateLrc(id);
        }

        if (type == "interaction") {
            priv->cpp->accountInfo_->conversationModel->selectConversation(information);
            conversations_view_select_conversation(CONVERSATIONS_VIEW(priv->treeview_conversations), information);
        } else if (type == "request") {
            for (const auto& conversation : priv->cpp->accountInfo_->conversationModel->getFilteredConversations(lrc::api::profile::Type::PENDING)) {
                auto contactRequestsPageNum = gtk_notebook_page_num(GTK_NOTEBOOK(priv->notebook_contacts),
                                                               priv->scrolled_window_contact_requests);
                gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook_contacts), contactRequestsPageNum);
                if (!conversation.participants.empty() && conversation.participants.front() == information) {
                    priv->cpp->accountInfo_->conversationModel->selectConversation(conversation.uid);
                }
                conversations_view_select_conversation(CONVERSATIONS_VIEW(priv->treeview_conversations), conversation.uid);
            }
        }
    } else {
        // accept or refuse notifiation
        try {
            auto& accountInfo = priv->cpp->lrc_->getAccountModel().getAccountInfo(id);
            for (const auto& conversation : accountInfo.conversationModel->getFilteredConversations(lrc::api::profile::Type::PENDING)) {
                if (!conversation.participants.empty() && conversation.participants.front() == information) {
                    if (action == Action::ACCEPT) {
                        accountInfo.conversationModel->makePermanent(conversation.uid);
                    } else {
                        accountInfo.conversationModel->removeConversation(conversation.uid);
                    }
                }
            }
        } catch (const std::out_of_range& e) {
            g_warning("Can't get account %s: %s", id.c_str(), e.what());
        }
    }

}

static void
on_notification_chat_clicked(G_GNUC_UNUSED GtkWidget* notifier,
                             gchar *title, RingMainWindow* self)
{
    action_notification(title, self, Action::SELECT);
}

static void
on_notification_accept_pending(GtkWidget*, gchar *title, RingMainWindow* self)
{
    action_notification(title, self, Action::ACCEPT);
}

static void
on_notification_refuse_pending(GtkWidget*, gchar *title, RingMainWindow* self)
{
    action_notification(title, self, Action::REFUSE);
}

static void
on_notification_accept_call(G_GNUC_UNUSED GtkWidget* notifier,
                            gchar *title, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self) && title);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    if (!priv->cpp->accountInfo_) {
        g_warning("Notification clicked but accountInfo_ currently empty");
        return;
    }

    std::string titleStr = title;
    auto firstMarker = titleStr.find(":");
    if (firstMarker == std::string::npos) return;
    auto secondMarker = titleStr.find(":", firstMarker + 1);
    if (secondMarker == std::string::npos) return;

    auto id = titleStr.substr(0, firstMarker);
    auto type = titleStr.substr(firstMarker + 1, secondMarker - firstMarker - 1);
    auto information = titleStr.substr(secondMarker + 1);

    if (priv->cpp->show_settings) {
        priv->cpp->leaveSettingsView();
    }

    try {
        auto& accountInfo = priv->cpp->lrc_->getAccountModel().getAccountInfo(id);
        accountInfo.callModel->accept(information);
    } catch (const std::out_of_range& e) {
        g_warning("Can't get account %s: %s", id.c_str(), e.what());
    }
}

static void
on_notification_decline_call(G_GNUC_UNUSED GtkWidget* notifier, gchar *title, RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self) && title);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    if (!priv->cpp->accountInfo_) {
        g_warning("Notification clicked but accountInfo_ currently empty");
        return;
    }

    std::string titleStr = title;
    auto firstMarker = titleStr.find(":");
    if (firstMarker == std::string::npos) return;
    auto secondMarker = titleStr.find(":", firstMarker + 1);
    if (secondMarker == std::string::npos) return;

    auto id = titleStr.substr(0, firstMarker);
    auto type = titleStr.substr(firstMarker + 1, secondMarker - firstMarker - 1);
    auto information = titleStr.substr(secondMarker + 1);

    try {
        auto& accountInfo = priv->cpp->lrc_->getAccountModel().getAccountInfo(id);
        accountInfo.callModel->hangUp(information);
    } catch (const std::out_of_range& e) {
        g_warning("Can't get account %s: %s", id.c_str(), e.what());
    }
}

} // namespace gtk_callbacks

CppImpl::CppImpl(RingMainWindow& widget)
    : self {&widget}
    , widgets {RING_MAIN_WINDOW_GET_PRIVATE(&widget)}
    , lrc_ {std::make_unique<lrc::api::Lrc>()}
{}

static gboolean
on_clear_all_history_foreach(GtkTreeModel *model, G_GNUC_UNUSED GtkTreePath *path, GtkTreeIter *iter, gpointer self)
{
    g_return_val_if_fail(IS_RING_MAIN_WINDOW(self), TRUE);

    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    gchar* account_id;

    gtk_tree_model_get(model, iter, 0 /* col# */, &account_id /* data */, -1);

    auto& accountInfo = priv->cpp->lrc_->getAccountModel().getAccountInfo(account_id);
    accountInfo.conversationModel->clearAllHistory();

    g_free(account_id);
    return FALSE;
}

static void
on_clear_all_history_clicked(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    auto accountComboBox = GTK_COMBO_BOX(priv->combobox_account_selector);
    auto model = gtk_combo_box_get_model(accountComboBox);

    gtk_tree_model_foreach (model, on_clear_all_history_foreach, self);

    priv->cpp->has_cleared_all_history = true;
}

static void
update_data_transfer(lrc::api::DataTransferModel& model, GSettings* settings)
{
    char* download_directory_value;
    auto* download_directory_variant = g_settings_get_value(settings, "download-folder");
    g_variant_get(download_directory_variant, "&s", &download_directory_value);
    std::string download_dir = download_directory_value;
    if (!download_dir.empty() && download_dir.back() != '/')
        download_dir += "/";
    model.downloadDirectory = download_dir;
}

static void
update_download_folder(RingMainWindow* self)
{
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    g_return_if_fail(priv);

    update_data_transfer(priv->cpp->lrc_->getDataTransferModel(), priv->settings);
}

void
CppImpl::init()
{
    // Remember the tabs page number for easier selection later
    smartviewPageNum = gtk_notebook_page_num(GTK_NOTEBOOK(widgets->notebook_contacts),
                                             widgets->scrolled_window_smartview);
    contactRequestsPageNum = gtk_notebook_page_num(GTK_NOTEBOOK(widgets->notebook_contacts),
                                                   widgets->scrolled_window_contact_requests);
    g_assert(smartviewPageNum != contactRequestsPageNum);

    // NOTE: When new models will be fully implemented, we need to move this
    // in rign_client.cpp->
    // Init LRC and the vew
    const auto accountIds = lrc_->getAccountModel().getAccountList();
    decltype(accountIds)::value_type activeAccountId; // non-empty if a enabled account is found below

    if (not accountIds.empty()) {
        on_handle_account_migrations(self);
        for (const auto& id : accountIds) {
            const auto& accountInfo = lrc_->getAccountModel().getAccountInfo(id);
            if (accountInfo.enabled) {
                activeAccountId = id;
                break;
            }
        }
    }

    if (!activeAccountId.empty()) {
        updateLrc(activeAccountId);
    } else {
        // No enabled account: create empty widgets
        widgets->treeview_conversations = conversations_view_new(accountInfo_);
        gtk_container_add(GTK_CONTAINER(widgets->scrolled_window_smartview), widgets->treeview_conversations);
        widgets->treeview_contact_requests = conversations_view_new(accountInfo_);
        gtk_container_add(GTK_CONTAINER(widgets->scrolled_window_contact_requests), widgets->treeview_contact_requests);
    }

    accountStatusChangedConnection_ = QObject::connect(&lrc_->getAccountModel(),
                                                       &lrc::api::NewAccountModel::accountStatusChanged,
                                                       [this](const std::string& id){ slotAccountStatusChanged(id); });
    profileUpdatedConnection_ = QObject::connect(&lrc_->getAccountModel(),
                                                 &lrc::api::NewAccountModel::profileUpdated,
                                                 [this](const std::string& id){
                                                     slotProfileUpdated(id);
                                                 });
    newAccountConnection_ = QObject::connect(&lrc_->getAccountModel(),
                                             &lrc::api::NewAccountModel::accountAdded,
                                             [this](const std::string& id){ slotAccountAddedFromLrc(id); });
    rmAccountConnection_ = QObject::connect(&lrc_->getAccountModel(),
                                            &lrc::api::NewAccountModel::accountRemoved,
                                            [this](const std::string& id){ slotAccountRemovedFromLrc(id); });
    invalidAccountConnection_ = QObject::connect(&lrc_->getAccountModel(),
                                                 &lrc::api::NewAccountModel::invalidAccountDetected,
                                                 [this](const std::string& id){ slotInvalidAccountFromLrc(id); });

    /* bind to window size settings */
    widgets->settings = g_settings_new_full(get_ring_schema(), nullptr, nullptr);
    auto width = g_settings_get_int(widgets->settings, "window-width");
    auto height = g_settings_get_int(widgets->settings, "window-height");
    gtk_window_set_default_size(GTK_WINDOW(self), width, height);
    g_signal_connect(self, "configure-event", G_CALLBACK(on_window_size_changed), nullptr);

    update_data_transfer(lrc_->getDataTransferModel(), widgets->settings);

    /* search-entry-places-call setting */
    on_search_entry_places_call_changed(widgets->settings, "search-entry-places-call", self);
    g_signal_connect(widgets->settings, "changed::search-entry-places-call",
                     G_CALLBACK(on_search_entry_places_call_changed), self);

    /* set window icon */
    GError *error = NULL;
    GdkPixbuf* icon = gdk_pixbuf_new_from_resource("/cx/ring/RingGnome/ring-symbol-blue", &error);
    if (icon == NULL) {
        g_debug("Could not load icon: %s", error->message);
        g_clear_error(&error);
    } else
        gtk_window_set_icon(GTK_WINDOW(self), icon);

    /* set menu icon */
    GdkPixbuf* image_ring = gdk_pixbuf_new_from_resource_at_scale("/cx/ring/RingGnome/ring-symbol-blue",
                                                                  -1, 16, TRUE, &error);
    if (image_ring == NULL) {
        g_debug("Could not load icon: %s", error->message);
        g_clear_error(&error);
    } else
        gtk_image_set_from_pixbuf(GTK_IMAGE(widgets->image_ring), image_ring);

    /* ring menu */
    GtkBuilder *builder = gtk_builder_new_from_resource("/cx/ring/RingGnome/ringgearsmenu.ui");
    GMenuModel *menu = G_MENU_MODEL(gtk_builder_get_object(builder, "menu"));
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(widgets->ring_menu), menu);
    g_object_unref(builder);

    /* settings icon */
    gtk_image_set_from_icon_name(GTK_IMAGE(widgets->image_settings), "emblem-system-symbolic",
                                 GTK_ICON_SIZE_SMALL_TOOLBAR);

    /* connect settings button signal */
    g_signal_connect_swapped(widgets->ring_settings, "clicked",
                             G_CALLBACK(on_settings_clicked), self);

    /* add the call view to the main stack */
    gtk_stack_add_named(GTK_STACK(widgets->stack_main_view), widgets->vbox_call_view,
                        CALL_VIEW_NAME);

    widgets->media_settings_view = media_settings_view_new();
    gtk_stack_add_named(GTK_STACK(widgets->stack_main_view), widgets->media_settings_view,
                        MEDIA_SETTINGS_VIEW_NAME);

    if (not accountIds.empty()) {
        widgets->new_account_settings_view = new_account_settings_view_new(accountInfo_);
        gtk_stack_add_named(GTK_STACK(widgets->stack_main_view), widgets->new_account_settings_view,
                            NEW_ACCOUNT_SETTINGS_VIEW_NAME);
    }

    widgets->general_settings_view = general_settings_view_new(GTK_WIDGET(self));
    widgets->update_download_folder = g_signal_connect_swapped(
        widgets->general_settings_view,
        "update-download-folder",
        G_CALLBACK(update_download_folder),
        self
    );
    gtk_stack_add_named(GTK_STACK(widgets->stack_main_view), widgets->general_settings_view,
                        GENERAL_SETTINGS_VIEW_NAME);
    g_signal_connect_swapped(widgets->general_settings_view, "clear-all-history", G_CALLBACK(on_clear_all_history_clicked), self);


    /* make the setting we will show first the active one */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->radiobutton_general_settings), TRUE);
    widgets->last_settings_view = widgets->general_settings_view;

    /* connect the settings button signals to switch settings views */
    g_signal_connect(widgets->radiobutton_media_settings, "toggled", G_CALLBACK(on_show_media_settings), self);
    g_signal_connect(widgets->radiobutton_general_settings, "toggled", G_CALLBACK(on_show_general_settings), self);
    g_signal_connect(widgets->radiobutton_new_account_settings, "toggled", G_CALLBACK(on_show_new_account_settings), self);
    g_signal_connect(widgets->notebook_contacts, "switch-page", G_CALLBACK(on_tab_changed), self);

    /* welcome/default view */
    widgets->welcome_view = ring_welcome_view_new(accountInfo_);
    g_object_ref(widgets->welcome_view); // increase ref because don't want it to be destroyed when not displayed
    gtk_container_add(GTK_CONTAINER(widgets->frame_call), widgets->welcome_view);
    gtk_widget_show(widgets->welcome_view);

    g_signal_connect_swapped(widgets->search_entry, "activate", G_CALLBACK(on_search_entry_activated), self);
    g_signal_connect_swapped(widgets->button_new_conversation, "clicked", G_CALLBACK(on_search_entry_activated), self);
    g_signal_connect(widgets->search_entry, "search-changed", G_CALLBACK(on_search_entry_text_changed), self);
    g_signal_connect(widgets->search_entry, "key-release-event", G_CALLBACK(on_search_entry_key_released), self);

    auto provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".search-entry-style { border: 0; border-radius: 0; } \
        .spinner-style { border: 0; background: white; } \
        .new-conversation-style { border: 0; background: white; transition: all 0.3s ease; border-radius: 0; } \
        .new-conversation-style:hover {  background: #bae5f0; }",
        -1, nullptr
    );
    gtk_style_context_add_provider_for_screen(gdk_display_get_default_screen(gdk_display_get_default()),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);


    /* react to digit key press events */
    g_signal_connect(self, "key-press-event", G_CALLBACK(on_dtmf_pressed), nullptr);

    /* set the search entry placeholder text */
    gtk_entry_set_placeholder_text(GTK_ENTRY(widgets->search_entry),
                                   C_("Please try to make the translation 50 chars or less so that it fits into the layout",
                                      "Search contacts or enter number"));

    /* init chat webkit container so that it starts loading before the first time we need it*/
    (void)webkitChatContainer();

    // setup account selector and select the first account
    refreshAccountSelectorWidget(0);

    /* layout */
    auto* renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgets->combobox_account_selector), renderer, true);
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(widgets->combobox_account_selector),
                                       renderer,
                                       (GtkCellLayoutDataFunc )render_account_avatar,
                                       widgets, nullptr);


    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(widgets->combobox_account_selector), renderer, true);
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(widgets->combobox_account_selector),
                                       renderer,
                                       (GtkCellLayoutDataFunc )print_account_and_state,
                                       widgets, nullptr);

    // we closing any view opened to avoid confusion (especially between SIP and Ring protocols).
    g_signal_connect_swapped(widgets->combobox_account_selector, "changed", G_CALLBACK(on_account_changed), self);

    // initialize the pending contact request icon.
    refreshPendingContactRequestTab();

    if (accountInfo_) {
        auto& conversationModel = accountInfo_->conversationModel;
        auto conversations = conversationModel->allFilteredConversations();
        for (const auto& conversation: conversations) {
            if (!conversation.callId.empty()) {
                accountInfo_->conversationModel->selectConversation(conversation.uid);
            }
        }
    }
    // delete obsolete history
    if (not accountIds.empty()) {
        auto days = g_settings_get_int(widgets->settings, "history-limit");
        for (auto& accountId : accountIds) {
            auto& accountInfo = lrc_->getAccountModel().getAccountInfo(accountId);
            accountInfo.conversationModel->deleteObsoleteHistory(days);
        }
    }

    // No account? Show wizard
    auto accounts = lrc_->getAccountModel().getAccountList();
    if (accounts.empty()) {
        enterAccountCreationWizard();
    }

    widgets->notif_chat_view = g_signal_connect(widgets->notifier, "showChatView",
                                                     G_CALLBACK(on_notification_chat_clicked), self);
    widgets->notif_accept_pending = g_signal_connect(widgets->notifier, "acceptPending",
                                                     G_CALLBACK(on_notification_accept_pending), self);
    widgets->notif_refuse_pending = g_signal_connect(widgets->notifier, "refusePending",
                                                    G_CALLBACK(on_notification_refuse_pending), self);
    widgets->notif_accept_call = g_signal_connect(widgets->notifier, "acceptCall",
                                                  G_CALLBACK(on_notification_accept_call), self);
    widgets->notif_decline_call = g_signal_connect(widgets->notifier, "declineCall",
                                                   G_CALLBACK(on_notification_decline_call), self);
}

CppImpl::~CppImpl()
{
    QObject::disconnect(showLeaveMessageViewConnection_);
    QObject::disconnect(showChatViewConnection_);
    QObject::disconnect(showIncomingViewConnection_);
    QObject::disconnect(historyClearedConnection_);
    QObject::disconnect(modelSortedConnection_);
    QObject::disconnect(callChangedConnection_);
    QObject::disconnect(newIncomingCallConnection_);
    QObject::disconnect(filterChangedConnection_);
    QObject::disconnect(newConversationConnection_);
    QObject::disconnect(conversationRemovedConnection_);
    QObject::disconnect(changeAccountConnection_);
    QObject::disconnect(newAccountConnection_);
    QObject::disconnect(rmAccountConnection_);
    QObject::disconnect(invalidAccountConnection_);
    QObject::disconnect(showCallViewConnection_);
    QObject::disconnect(newTrustRequestNotification_);
    QObject::disconnect(closeTrustRequestNotification_);
    QObject::disconnect(slotNewInteraction_);
    QObject::disconnect(slotReadInteraction_);
    QObject::disconnect(accountStatusChangedConnection_);
    QObject::disconnect(profileUpdatedConnection_);

    g_clear_object(&widgets->welcome_view);
    g_clear_object(&widgets->webkit_chat_container);
}

void
CppImpl::changeView(GType type, lrc::api::conversation::Info conversation)
{
    leaveFullScreen();
    gtk_container_remove(GTK_CONTAINER(widgets->frame_call),
                         gtk_bin_get_child(GTK_BIN(widgets->frame_call)));

    GtkWidget* new_view;
    if (g_type_is_a(INCOMING_CALL_VIEW_TYPE, type)) {
        new_view = displayIncomingView(conversation);
    } else if (g_type_is_a(CURRENT_CALL_VIEW_TYPE, type)) {
        new_view = displayCurrentCallView(conversation);
    } else if (g_type_is_a(CHAT_VIEW_TYPE, type)) {
        new_view = displayChatView(conversation);
    } else {
        new_view = displayWelcomeView(conversation);
    }

    gtk_container_add(GTK_CONTAINER(widgets->frame_call), new_view);
    gtk_widget_show(new_view);
}

GtkWidget*
CppImpl::displayWelcomeView(lrc::api::conversation::Info conversation)
{
    (void) conversation;

    // TODO select first conversation?

    chatViewConversation_.reset(nullptr);
    refreshPendingContactRequestTab();

    return widgets->welcome_view;;
}

GtkWidget*
CppImpl::displayIncomingView(lrc::api::conversation::Info conversation)
{
    chatViewConversation_.reset(new lrc::api::conversation::Info(conversation));
    return incoming_call_view_new(webkitChatContainer(), lrc_->getAVModel(), accountInfo_, chatViewConversation_.get());
}

GtkWidget*
CppImpl::displayCurrentCallView(lrc::api::conversation::Info conversation)
{
    chatViewConversation_.reset(new lrc::api::conversation::Info(conversation));
    auto* new_view = current_call_view_new(webkitChatContainer(),
                                           accountInfo_, chatViewConversation_.get());

    try {
        auto contactUri = chatViewConversation_->participants.front();
        auto contactInfo = accountInfo_->contactModel->getContact(contactUri);
        if (auto chat_view = current_call_view_get_chat_view(CURRENT_CALL_VIEW(new_view))) {
            chat_view_update_temporary(CHAT_VIEW(chat_view));
        }
    } catch(...) { }

    g_signal_connect_swapped(new_view, "video-double-clicked",
                             G_CALLBACK(on_video_double_clicked), self);
    return new_view;
}

GtkWidget*
CppImpl::displayChatView(lrc::api::conversation::Info conversation)
{
    chatViewConversation_.reset(new lrc::api::conversation::Info(conversation));
    auto* new_view = chat_view_new(webkitChatContainer(), accountInfo_, chatViewConversation_.get());
    g_signal_connect_swapped(new_view, "hide-view-clicked", G_CALLBACK(on_hide_view_clicked), self);
    try {
        auto contactUri = chatViewConversation_->participants.front();
        auto contactInfo = accountInfo_->contactModel->getContact(contactUri);
        auto isPending = contactInfo.profileInfo.type == lrc::api::profile::Type::PENDING;
        if (isPending) {
            auto notifId = accountInfo_->id + ":request:" + contactUri;
            ring_hide_notification(RING_NOTIFIER(widgets->notifier), notifId);
        }
    } catch(...) { }

    return new_view;
}

WebKitChatContainer*
CppImpl::webkitChatContainer() const
{
    if (!widgets->webkit_chat_container) {
        widgets->webkit_chat_container = webkit_chat_container_new();

        // We don't want it to be deleted, ever.
        g_object_ref(widgets->webkit_chat_container);
    }
    return WEBKIT_CHAT_CONTAINER(widgets->webkit_chat_container);
}

void
CppImpl::enterFullScreen()
{
    if (!is_fullscreen) {
        gtk_widget_hide(widgets->vbox_left_pane);
        gtk_window_fullscreen(GTK_WINDOW(self));
        is_fullscreen = true;
    }
}

void
CppImpl::leaveFullScreen()
{
    if (is_fullscreen) {
        gtk_widget_show(widgets->vbox_left_pane);
        gtk_window_unfullscreen(GTK_WINDOW(self));
        is_fullscreen = false;
    }
}

void
CppImpl::toggleFullScreen()
{
    if (is_fullscreen)
        leaveFullScreen();
    else
        enterFullScreen();
}

/// Clear selection and go to welcome page.
void
CppImpl::resetToWelcome()
{
    auto selection_conversations = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgets->treeview_conversations));
    gtk_tree_selection_unselect_all(GTK_TREE_SELECTION(selection_conversations));
    auto selection_contact_request = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgets->treeview_contact_requests));
    gtk_tree_selection_unselect_all(GTK_TREE_SELECTION(selection_contact_request));
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    lrc::api::conversation::Info current_item;
    if (IS_CHAT_VIEW(old_view))
        current_item = chat_view_get_conversation(CHAT_VIEW(old_view));
    changeView(RING_WELCOME_VIEW_TYPE, current_item);
}

void
CppImpl::refreshPendingContactRequestTab()
{
    if (!accountInfo_)
        return;

    auto hasPendingRequests = accountInfo_->contactModel->hasPendingRequests();
    gtk_widget_set_visible(widgets->scrolled_window_contact_requests, hasPendingRequests);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(widgets->notebook_contacts), hasPendingRequests);

    // show conversation page if PendingRequests list is empty
    if (not hasPendingRequests) {
        auto current_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widgets->notebook_contacts));
        if (current_page == contactRequestsPageNum)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(widgets->notebook_contacts), smartviewPageNum);
    }
}

void
CppImpl::showAccountSelectorWidget(bool show)
{
    gtk_widget_set_visible(widgets->combobox_account_selector, show);
}

/// Update the account GtkComboBox from LRC data and select the given entry.
/// The widget visibility is changed depending on number of account found.
/// /note Default selection_row is -1, meaning no selection.
/// /note Default selected is "", meaning no forcing
std::size_t
CppImpl::refreshAccountSelectorWidget(int selection_row, const std::string& selected)
{
    auto store = gtk_list_store_new(6 /* # of cols */ ,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_UINT);
    GtkTreeIter iter;
    std::size_t enabled_accounts = 0;
    std::size_t idx = 0;
    foreachLrcAccount(*lrc_, [&] (const auto& acc_info) {
            ++enabled_accounts;
            if (!selected.empty() && selected == acc_info.id) {
                selection_row = idx;
            }
            gtk_list_store_append(store, &iter);
            std::string status = "";
            switch (acc_info.status) {
                case lrc::api::account::Status::INVALID:
                case lrc::api::account::Status::ERROR_NEED_MIGRATION:
                case lrc::api::account::Status::UNREGISTERED:
                    status = "DISCONNECTED";
                    break;
                case lrc::api::account::Status::INITIALIZING:
                case lrc::api::account::Status::TRYING:
                    status = "TRYING";
                    break;
                case lrc::api::account::Status::REGISTERED:
                    status = "CONNECTED";
                    break;
            }
            gtk_list_store_set(store, &iter,
                               0 /* col # */ , acc_info.id.c_str() /* celldata */,
                               1 /* col # */ , status.c_str() /* celldata */,
                               2 /* col # */ , acc_info.profileInfo.avatar.c_str() /* celldata */,
                               3 /* col # */ , acc_info.profileInfo.uri.c_str() /* celldata */,
                               4 /* col # */ , acc_info.profileInfo.alias.c_str() /* celldata */,
                               5 /* col # */ , acc_info.registeredName.c_str() /* celldata */,
                               -1 /* end */);
            ++idx;
        });

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       0 /* col # */ , "" /* celldata */,
                       1 /* col # */ , "" /* celldata */,
                       2 /* col # */ , "" /* celldata */,
                       3 /* col # */ , "" /* celldata */,
                       4 /* col # */ , "" /* celldata */,
                       5 /* col # */ , "" /* celldata */,
                       -1 /* end */);

    gtk_combo_box_set_model(
        GTK_COMBO_BOX(widgets->combobox_account_selector),
        GTK_TREE_MODEL(store)
    );
    widgets->set_top_account_flag = false;
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->combobox_account_selector), selection_row);

    return enabled_accounts;
}

void
CppImpl::enterAccountCreationWizard(bool showControls)
{
    if (!widgets->account_creation_wizard) {
        widgets->account_creation_wizard = account_creation_wizard_new(false);
        g_object_add_weak_pointer(G_OBJECT(widgets->account_creation_wizard),
                                  reinterpret_cast<gpointer*>(&widgets->account_creation_wizard));
        g_signal_connect_swapped(widgets->account_creation_wizard, "account-creation-completed",
                                 G_CALLBACK(on_account_creation_completed), self);

        gtk_stack_add_named(GTK_STACK(widgets->stack_main_view),
                            widgets->account_creation_wizard,
                            ACCOUNT_CREATION_WIZARD_VIEW_NAME);
    }

    /* hide settings button until account creation is complete */
    gtk_widget_hide(widgets->hbox_settings);
    gtk_widget_hide(widgets->ring_settings);
    showAccountSelectorWidget(showControls);

    gtk_widget_show(widgets->account_creation_wizard);
    gtk_stack_set_visible_child(GTK_STACK(widgets->stack_main_view), widgets->account_creation_wizard);
}

void
CppImpl::leaveAccountCreationWizard()
{
    if (show_settings) {
        gtk_stack_set_visible_child(GTK_STACK(widgets->stack_main_view), widgets->last_settings_view);
        gtk_widget_show(widgets->hbox_settings);
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(widgets->stack_main_view), CALL_VIEW_NAME);
    }

    /* destroy the wizard */
    if (widgets->account_creation_wizard) {
        gtk_container_remove(GTK_CONTAINER(widgets->stack_main_view), widgets->account_creation_wizard);
        gtk_widget_destroy(widgets->account_creation_wizard);
        widgets->account_creation_wizard = nullptr;
    }

    /* show the settings button */
    gtk_widget_show(widgets->ring_settings);

    /* show the account selector */
    showAccountSelectorWidget();
}

/// Change the selection of the account ComboBox by account Id.
/// Find in displayed accounts with one corresponding to the given id, then select it
void
CppImpl::changeAccountSelection(const std::string& id)
{
    // already selected?
    if (id == accountInfo_->id)
        return;

    if (auto* model = gtk_combo_box_get_model(GTK_COMBO_BOX(widgets->combobox_account_selector))) {
        GtkTreeIter iter;
        auto valid = gtk_tree_model_get_iter_first(model, &iter);
        while (valid) {
            gchar* account_id;
            gtk_tree_model_get(model, &iter, 0 /* col# */, &account_id /* data */, -1);
            if (id == account_id) {
                widgets->set_top_account_flag = false;
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(widgets->combobox_account_selector), &iter);
                g_free(account_id);
                return;
            }
            valid = gtk_tree_model_iter_next(model, &iter);
            g_free(account_id);
        }

        g_debug("BUGS: account not listed: %s", id.c_str());
    }
}

void
CppImpl::onAccountSelectionChange(const std::string& id)
{
    auto oldId = accountInfo_? accountInfo_->id : "";

    if (id != oldId) {
        // Go to welcome view
        changeView(RING_WELCOME_VIEW_TYPE);
        // Show conversation panel
        gtk_notebook_set_current_page(GTK_NOTEBOOK(widgets->notebook_contacts), 0);
        // Reinit LRC
        updateLrc(id);
        // Update the welcome view
        ring_welcome_update_view(RING_WELCOME_VIEW(widgets->welcome_view));
        // Show pending contacts tab if necessary
        refreshPendingContactRequestTab();
        // Update account settings
        if (widgets->new_account_settings_view)
            new_account_settings_view_update(NEW_ACCOUNT_SETTINGS_VIEW(widgets->new_account_settings_view), true);
    }
}

void
CppImpl::enterSettingsView()
{
    /* show the settings */
    show_settings = true;

    /* show settings */
    gtk_image_set_from_icon_name(GTK_IMAGE(widgets->image_settings), "go-previous-symbolic",
                                 GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(GTK_WIDGET(widgets->ring_settings), _("Leave settings page"));

    gtk_widget_show(widgets->hbox_settings);

    /* make sure to start preview if we're showing the video settings */
    if (widgets->last_settings_view == widgets->media_settings_view)
        media_settings_view_show_preview(MEDIA_SETTINGS_VIEW(widgets->media_settings_view), TRUE);

    /* make sure avatar widget is restored properly if we're showing general settings */
    if (widgets->last_settings_view == widgets->new_account_settings_view)
        new_account_settings_view_show(NEW_ACCOUNT_SETTINGS_VIEW(widgets->new_account_settings_view),
                                       TRUE);

    gtk_stack_set_visible_child(GTK_STACK(widgets->stack_main_view), widgets->last_settings_view);
}

void
CppImpl::leaveSettingsView()
{
    /* hide the settings */
    show_settings = false;

    AccountModel::instance().save();

    /* show calls */
    gtk_image_set_from_icon_name(GTK_IMAGE(widgets->image_settings), "emblem-system-symbolic",
                                 GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(GTK_WIDGET(widgets->ring_settings), _("Settings"));

    gtk_widget_hide(widgets->hbox_settings);

    /* make sure video preview is stopped, in case it was started */
    media_settings_view_show_preview(MEDIA_SETTINGS_VIEW(widgets->media_settings_view), FALSE);
    new_account_settings_view_show(NEW_ACCOUNT_SETTINGS_VIEW(widgets->new_account_settings_view),
                                   FALSE);

    gtk_stack_set_visible_child_name(GTK_STACK(widgets->stack_main_view), CALL_VIEW_NAME);

    /* return to the welcome view if has_cleared_all_history. The reason is you can have been in a chatview before you
     * opened the settings view and did a clear all history. So without the code below, you'll see the chatview with
     * obsolete messages. It will also ensure to refresh last interaction printed in the conversations list.
     */
    if (has_cleared_all_history) {
        onAccountSelectionChange(accountInfo_->id);
        resetToWelcome();
        has_cleared_all_history = false;
    }
}

void
CppImpl::updateLrc(const std::string& id, const std::string& accountIdToFlagFreeable)
{
    // Disconnect old signals.
    QObject::disconnect(showLeaveMessageViewConnection_);
    QObject::disconnect(showChatViewConnection_);
    QObject::disconnect(showIncomingViewConnection_);
    QObject::disconnect(changeAccountConnection_);
    QObject::disconnect(showCallViewConnection_);
    QObject::disconnect(newTrustRequestNotification_);
    QObject::disconnect(closeTrustRequestNotification_);
    QObject::disconnect(slotNewInteraction_);
    QObject::disconnect(slotReadInteraction_);
    QObject::disconnect(modelSortedConnection_);
    QObject::disconnect(callChangedConnection_);
    QObject::disconnect(newIncomingCallConnection_);
    QObject::disconnect(historyClearedConnection_);
    QObject::disconnect(filterChangedConnection_);
    QObject::disconnect(newConversationConnection_);
    QObject::disconnect(conversationRemovedConnection_);

    if (!accountIdToFlagFreeable.empty()) {
        g_debug("Account %s flagged for removal. Mark it freeable.", accountIdToFlagFreeable.c_str());
        try {
            lrc_->getAccountModel().flagFreeable(accountIdToFlagFreeable);
        } catch (std::exception& e) {
            g_debug("Error while flagging %s for removal: '%s'.", accountIdToFlagFreeable.c_str(), e.what());
            g_debug("This is most likely an internal error, please report this bug.");
        } catch (...) {
            g_debug("Unexpected failure while flagging %s for removal.", accountIdToFlagFreeable.c_str());
        }
    }

    // Get the account selected
    if (!id.empty())
        accountInfo_ = &lrc_->getAccountModel().getAccountInfo(id);
    else
        accountInfo_ = nullptr;

    // Reinit tree views
    if (widgets->treeview_conversations) {
        auto selection_conversations = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgets->treeview_conversations));
        gtk_tree_selection_unselect_all(GTK_TREE_SELECTION(selection_conversations));
        gtk_widget_destroy(widgets->treeview_conversations);
    }
    widgets->treeview_conversations = conversations_view_new(accountInfo_);
    gtk_container_add(GTK_CONTAINER(widgets->scrolled_window_smartview), widgets->treeview_conversations);

    if (widgets->treeview_contact_requests) {
        auto selection_conversations = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgets->treeview_contact_requests));
        gtk_tree_selection_unselect_all(GTK_TREE_SELECTION(selection_conversations));
        gtk_widget_destroy(widgets->treeview_contact_requests);
    }
    widgets->treeview_contact_requests = conversations_view_new(accountInfo_);
    gtk_container_add(GTK_CONTAINER(widgets->scrolled_window_contact_requests), widgets->treeview_contact_requests);

    if (!accountInfo_) return;

    // Connect to signals from LRC
    historyClearedConnection_ = QObject::connect(&*accountInfo_->conversationModel,
                                                 &lrc::api::ConversationModel::conversationCleared,
                                                 [this] (const std::string& id) { slotConversationCleared(id); });

    modelSortedConnection_ = QObject::connect(&*accountInfo_->conversationModel,
                                              &lrc::api::ConversationModel::modelSorted,
                                              [this] { slotModelSorted(); });

    callChangedConnection_ = QObject::connect(&*accountInfo_->callModel,
                                              &lrc::api::NewCallModel::callStatusChanged,
                                              [this] (const std::string& callId) { slotCallStatusChanged(callId); });

    newIncomingCallConnection_ = QObject::connect(&*accountInfo_->callModel,
                                                  &lrc::api::NewCallModel::newIncomingCall,
                                                  [this] (const std::string&, const std::string& callId) { slotNewIncomingCall(callId); });

    filterChangedConnection_ = QObject::connect(&*accountInfo_->conversationModel,
                                                &lrc::api::ConversationModel::filterChanged,
                                                [this] { slotFilterChanged(); });

    newConversationConnection_ = QObject::connect(&*accountInfo_->conversationModel,
                                                  &lrc::api::ConversationModel::newConversation,
                                                  [this] (const std::string& id) { slotNewConversation(id); });

    conversationRemovedConnection_ = QObject::connect(&*accountInfo_->conversationModel,
                                                      &lrc::api::ConversationModel::conversationRemoved,
                                                      [this] (const std::string& id) { slotConversationRemoved(id); });

    showChatViewConnection_ = QObject::connect(&lrc_->getBehaviorController(),
                                               &lrc::api::BehaviorController::showChatView,
                                               [this] (const std::string& id, lrc::api::conversation::Info origin) { slotShowChatView(id, origin); });

    showLeaveMessageViewConnection_ = QObject::connect(&lrc_->getBehaviorController(),
                                               &lrc::api::BehaviorController::showLeaveMessageView,
                                               [this] (const std::string&, lrc::api::conversation::Info conv) { slotShowLeaveMessageView(conv); });

    showCallViewConnection_ = QObject::connect(&lrc_->getBehaviorController(),
                                               &lrc::api::BehaviorController::showCallView,
                                               [this] (const std::string& id, lrc::api::conversation::Info origin) { slotShowCallView(id, origin); });

    newTrustRequestNotification_ = QObject::connect(&lrc_->getBehaviorController(),
                                                    &lrc::api::BehaviorController::newTrustRequest,
                                                    [this] (const std::string& id, const std::string& contactUri) { slotNewTrustRequest(id, contactUri); });

    closeTrustRequestNotification_ = QObject::connect(&lrc_->getBehaviorController(),
                                                      &lrc::api::BehaviorController::trustRequestTreated,
                                                      [this] (const std::string& id, const std::string& contactUri) { slotCloseTrustRequest(id, contactUri); });

    showIncomingViewConnection_ = QObject::connect(&lrc_->getBehaviorController(),
                                                   &lrc::api::BehaviorController::showIncomingCallView,
                                                   [this] (const std::string& id, lrc::api::conversation::Info origin)
                                                          { slotShowIncomingCallView(id, origin); });

    slotNewInteraction_ = QObject::connect(&lrc_->getBehaviorController(),
                                           &lrc::api::BehaviorController::newUnreadInteraction,
                                           [this] (const std::string& accountId, const std::string& conversation,
                                                  uint64_t interactionId, const lrc::api::interaction::Info& interaction)
                                                  { slotNewInteraction(accountId, conversation, interactionId, interaction); });

    slotReadInteraction_ = QObject::connect(&lrc_->getBehaviorController(),
                                            &lrc::api::BehaviorController::newReadInteraction,
                                            [this] (const std::string& accountId, const std::string& conversation, uint64_t interactionId)
                                                   { slotCloseInteraction(accountId, conversation, interactionId); });

    const gchar *text = gtk_entry_get_text(GTK_ENTRY(widgets->search_entry));
    currentTypeFilter_ = accountInfo_->profileInfo.type;
    accountInfo_->conversationModel->setFilter(text);
    accountInfo_->conversationModel->setFilter(currentTypeFilter_);
}

void
CppImpl::slotAccountAddedFromLrc(const std::string& id)
{
    auto currentIdx = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->combobox_account_selector));
    if (currentIdx == -1)
        currentIdx = 0; // If no account selected, select the first account

    try {
        auto& account_model = lrc_->getAccountModel();

        const auto& account_info = account_model.getAccountInfo(id);
        auto old_view = gtk_stack_get_visible_child(GTK_STACK(widgets->stack_main_view));
        if(IS_ACCOUNT_CREATION_WIZARD(old_view)) {
            // TODO finalize (set avatar + register name)
            account_creation_wizard_account_added(ACCOUNT_CREATION_WIZARD(old_view), &account_info);
        }
        if (!accountInfo_) {
            updateLrc(id);
            if (!gtk_stack_get_child_by_name(GTK_STACK(widgets->stack_main_view), NEW_ACCOUNT_SETTINGS_VIEW_NAME)) {
                widgets->new_account_settings_view = new_account_settings_view_new(accountInfo_);
                gtk_stack_add_named(GTK_STACK(widgets->stack_main_view), widgets->new_account_settings_view,
                                    NEW_ACCOUNT_SETTINGS_VIEW_NAME);
            }
        }
        refreshAccountSelectorWidget(currentIdx, id);
        if (account_info.profileInfo.type == lrc::api::profile::Type::SIP) {
            enterSettingsView();
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->radiobutton_new_account_settings), true);
        }
    } catch (...) {
        refreshAccountSelectorWidget(currentIdx, id);
    }
}

void
CppImpl::slotAccountRemovedFromLrc(const std::string& id)
{
    /* Before doing anything, we need to update the struct pointers
       and tell the LRC it can free the old structures. */
    updateLrc("", id);

    auto accounts = lrc_->getAccountModel().getAccountList();
    if (accounts.empty()) {
        leaveSettingsView();
        enterAccountCreationWizard();
        return;
    }

    /* Update Account selector. This will trigger an update of the LRC
       and of all elements of the main view, we don't need to do it
       manually here. */
    refreshAccountSelectorWidget(0);
}

void
CppImpl::slotInvalidAccountFromLrc(const std::string& id)
{
    auto old_view = gtk_stack_get_visible_child(GTK_STACK(widgets->stack_main_view));
    if(IS_ACCOUNT_CREATION_WIZARD(old_view)) {
        account_creation_show_error_view(ACCOUNT_CREATION_WIZARD(old_view), id);
    }
}

lrc::api::conversation::Info
CppImpl::getCurrentConversation(GtkWidget* frame_call)
{
    lrc::api::conversation::Info current_item;
    current_item.uid = "-1";
    if (IS_CHAT_VIEW(frame_call)) {
        current_item = chat_view_get_conversation(CHAT_VIEW(frame_call));
    } else if (IS_CURRENT_CALL_VIEW(frame_call)) {
        current_item = current_call_view_get_conversation(CURRENT_CALL_VIEW(frame_call));
    } else if (IS_INCOMING_CALL_VIEW(frame_call)) {
        current_item = incoming_call_view_get_conversation(INCOMING_CALL_VIEW(frame_call));
    }

    return current_item;
}

void
CppImpl::slotAccountStatusChanged(const std::string& id)
{
    if (!accountInfo_) {
        updateLrc(id);
        ring_welcome_update_view(RING_WELCOME_VIEW(widgets->welcome_view));
        return;
    }

    new_account_settings_view_update(NEW_ACCOUNT_SETTINGS_VIEW(widgets->new_account_settings_view), false);
    auto currentIdx = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->combobox_account_selector));
    if (currentIdx == -1)
        currentIdx = 0; // If no account selected, select the first account
    refreshAccountSelectorWidget(currentIdx);

    auto* frame_call = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    conversations_view_select_conversation(CONVERSATIONS_VIEW(widgets->treeview_conversations), getCurrentConversation(frame_call).uid);

    if (IS_CHAT_VIEW(frame_call)) {
        chat_view_update_temporary(CHAT_VIEW(frame_call));
    }
}

void
CppImpl::slotConversationCleared(const std::string& uid)
{
    // Change the view when the history is cleared.
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    g_return_if_fail(IS_CHAT_VIEW(old_view));

    lrc::api::conversation::Info current_item = getCurrentConversation(old_view);

    if (current_item.uid == uid) {
        // We are on the conversation cleared.
        // Go to welcome view because user doesn't want this conversation
        // TODO go to first conversation?
        resetToWelcome();
    }
}

void
CppImpl::slotModelSorted()
{
    // Synchronize selection when sorted and update pending icon
    auto* frame_call = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    conversations_view_select_conversation(CONVERSATIONS_VIEW(widgets->treeview_conversations), getCurrentConversation(frame_call).uid);
    refreshPendingContactRequestTab();
}

void
CppImpl::slotCallStatusChanged(const std::string& callId)
{
    if (!accountInfo_) {
        return;
    }
    try {
        auto call = accountInfo_->callModel->getCall(callId);
        auto peer = call.peer;
        if (accountInfo_->profileInfo.type == lrc::api::profile::Type::RING && peer.find("ring:") == 0) {
            peer = peer.substr(5);
        }
        std::string notifId = "";
        try {
            notifId = accountInfo_->id + ":call:" + callId;
        } catch (...) {
            g_warning("Can't get contact for account %s. Don't show notification", accountInfo_->id.c_str());
            return;
        }

        if (call.status == lrc::api::call::Status::IN_PROGRESS
            || call.status == lrc::api::call::Status::ENDED) {
            // Call ended, close the notification
            ring_hide_notification(RING_NOTIFIER(widgets->notifier), notifId);
        }
    } catch (const std::exception& e) {
        g_warning("Can't get call %s for this account.", callId.c_str());
    }
}

void
CppImpl::slotNewIncomingCall(const std::string& callId)
{
    if (!accountInfo_) {
        return;
    }
    try {
        auto call = accountInfo_->callModel->getCall(callId);
        auto peer = call.peer;
        if (accountInfo_->profileInfo.type == lrc::api::profile::Type::RING && peer.find("ring:") == 0) {
            peer = peer.substr(5);
        }
        auto& contactModel = accountInfo_->contactModel;
        std::string avatar = "", name = "", notifId = "", uri = "";
        try {
            auto contactInfo = contactModel->getContact(peer);
            uri = contactInfo.profileInfo.uri;
            avatar = contactInfo.profileInfo.avatar;
            name = contactInfo.profileInfo.alias;
            if (name.empty()) {
                name = contactInfo.registeredName;
                if (name.empty()) {
                    name = contactInfo.profileInfo.uri;
                }
            }
            notifId = accountInfo_->id + ":call:" + callId;
        } catch (...) {
            g_warning("Can't get contact for account %s. Don't show notification", accountInfo_->id.c_str());
            return;
        }

        if (g_settings_get_boolean(widgets->settings, "enable-call-notifications")) {
            name.erase(std::remove(name.begin(), name.end(), '\r'), name.end());
            auto body = name + _(" is calling you!");
            ring_show_notification(RING_NOTIFIER(widgets->notifier), avatar, uri, name, notifId, _("Incoming call"), body, NotificationType::CALL);
        }
    } catch (const std::exception& e) {
        g_warning("Can't get call %s for this account.", callId.c_str());
    }
}

void
CppImpl::slotFilterChanged()
{
    // Synchronize selection when filter changes
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    auto current_item = getCurrentConversation(old_view);
    conversations_view_select_conversation(CONVERSATIONS_VIEW(widgets->treeview_conversations), current_item.uid);

    // Get if conversation still exists.
    auto& conversationModel = accountInfo_->conversationModel;
    auto conversations = conversationModel->allFilteredConversations();
    auto conversation = std::find_if(
        conversations.begin(), conversations.end(),
        [current_item](const lrc::api::conversation::Info& conversation) {
            return current_item.uid == conversation.uid;
        });
    bool isInConv = conversation == conversations.end();

    if (IS_CHAT_VIEW(old_view)) {
        if (isInConv) {
            changeView(RING_WELCOME_VIEW_TYPE);
        } else {
            /* Refresh chat view. In some cases (like when a contact is unbanned)
               a changing filter also implies the need of redrawing the chat */
            changeView(CHAT_VIEW_TYPE, *conversation);
        }
    }
}

void
CppImpl::slotNewConversation(const std::string& uid)
{
    gtk_notebook_set_current_page(GTK_NOTEBOOK(widgets->notebook_contacts), 0);
    accountInfo_->conversationModel->setFilter(lrc::api::profile::Type::RING);
    gtk_entry_set_text(GTK_ENTRY(widgets->search_entry), "");
    accountInfo_->conversationModel->setFilter("");
    // Select new conversation if contact added
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    if (IS_RING_WELCOME_VIEW(old_view)) {
        accountInfo_->conversationModel->selectConversation(uid);
        try {
            auto contactUri =  chatViewConversation_->participants.front();
            auto contactInfo = accountInfo_->contactModel->getContact(contactUri);
            chat_view_update_temporary(CHAT_VIEW(gtk_bin_get_child(GTK_BIN(widgets->frame_call))));
        } catch(...) { }
    }
}

void
CppImpl::slotConversationRemoved(const std::string& uid)
{
    // If contact is removed, go to welcome view
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    lrc::api::conversation::Info current_item;
    if (IS_CHAT_VIEW(old_view))
        current_item = chat_view_get_conversation(CHAT_VIEW(old_view));
    else if (IS_CURRENT_CALL_VIEW(old_view))
        current_item = current_call_view_get_conversation(CURRENT_CALL_VIEW(old_view));
    else if (IS_INCOMING_CALL_VIEW(old_view))
        current_item = incoming_call_view_get_conversation(INCOMING_CALL_VIEW(old_view));
    if (current_item.uid == uid)
        changeView(RING_WELCOME_VIEW_TYPE);
}

void
CppImpl::slotShowChatView(const std::string& id, lrc::api::conversation::Info origin)
{
    changeAccountSelection(id);
    // Show chat view if not in call (unless if it's the same conversation)
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    lrc::api::conversation::Info current_item;
    current_item.uid = "-1";
    if (IS_CHAT_VIEW(old_view))
        current_item = chat_view_get_conversation(CHAT_VIEW(old_view));
    // Do not show a conversation without any participants
    if (origin.participants.empty()) return;
    auto firstContactUri = origin.participants.front();
    auto contactInfo = accountInfo_->contactModel->getContact(firstContactUri);
    // change view if necessary or just update temporary
    if (current_item.uid != origin.uid) {
        changeView(CHAT_VIEW_TYPE, origin);
    } else {
        chat_view_update_temporary(CHAT_VIEW(old_view));
    }
}

void
CppImpl::slotShowLeaveMessageView(lrc::api::conversation::Info conv)
{
    auto* current_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));
    if (IS_INCOMING_CALL_VIEW(current_view)) {
        incoming_call_view_let_a_message(INCOMING_CALL_VIEW(current_view), conv);
    }
}

void
CppImpl::slotShowCallView(const std::string& id, lrc::api::conversation::Info origin)
{
    changeAccountSelection(id);
    // Change the view if we want a different view.
    auto* old_view = gtk_bin_get_child(GTK_BIN(widgets->frame_call));

    lrc::api::conversation::Info current_item;
    if (IS_CURRENT_CALL_VIEW(old_view))
        current_item = current_call_view_get_conversation(CURRENT_CALL_VIEW(old_view));

    if (current_item.uid != origin.uid)
        changeView(CURRENT_CALL_VIEW_TYPE, origin);
}

void
CppImpl::slotNewTrustRequest(const std::string& id, const std::string& contactUri)
{
    try {
        auto& accountInfo = lrc_->getAccountModel().getAccountInfo(id);
        auto notifId = accountInfo.id + ":request:" + contactUri;
        std::string avatar = "", name = "", uri = "";
        auto& contactModel = accountInfo.contactModel;
        try {
            auto contactInfo = contactModel->getContact(contactUri);
            uri = contactInfo.profileInfo.uri;
            avatar = contactInfo.profileInfo.avatar;
            name = contactInfo.profileInfo.alias;
            if (name.empty()) {
                name = contactInfo.registeredName;
                if (name.empty()) {
                    name = contactInfo.profileInfo.uri;
                }
            }
        } catch (...) {
            g_warning("Can't get contact for account %s. Don't show notification", accountInfo.id.c_str());
            return;
        }
        if (g_settings_get_boolean(widgets->settings, "enable-pending-notifications")) {
            name.erase(std::remove(name.begin(), name.end(), '\r'), name.end());
            auto body = _("New request from ") + name;
            ring_show_notification(RING_NOTIFIER(widgets->notifier), avatar, uri, name, notifId, _("Trust request"), body, NotificationType::REQUEST);
        }
    } catch (...) {
        g_warning("Can't get account %s", id.c_str());
    }
}

void
CppImpl::slotCloseTrustRequest(const std::string& id, const std::string& contactUri)
{
    try {
        auto& accountInfo = lrc_->getAccountModel().getAccountInfo(id);
        auto notifId = accountInfo.id + ":request:" + contactUri;
        ring_hide_notification(RING_NOTIFIER(widgets->notifier), notifId);
    } catch (...) {
        g_warning("Can't get account %s", id.c_str());
    }
}

void
CppImpl::slotNewInteraction(const std::string& accountId, const std::string& conversation,
                            uint64_t, const lrc::api::interaction::Info& interaction)
{
    if (chatViewConversation_ && chatViewConversation_->uid == conversation) {
        if (gtk_window_is_active(GTK_WINDOW(self))) {
            return;
        }
    }
    try {
        auto& accountInfo = lrc_->getAccountModel().getAccountInfo(accountId);
        auto notifId = accountInfo.id + ":interaction:" + conversation;
        auto& contactModel = accountInfo.contactModel;
        auto& conversationModel = accountInfo.conversationModel;
        for (const auto& conv : conversationModel->allFilteredConversations())
        {
            if (conv.uid == conversation) {
                if (conv.participants.empty()) return;
                std::string avatar = "", name = "", uri = "";
                try {
                    auto contactInfo = contactModel->getContact(conv.participants.front());
                    uri = contactInfo.profileInfo.uri;
                    avatar = contactInfo.profileInfo.avatar;
                    name = contactInfo.profileInfo.alias;
                    if (name.empty()) {
                        name = contactInfo.registeredName;
                        if (name.empty()) {
                            name = contactInfo.profileInfo.uri;
                        }
                    }
                } catch (...) {
                    g_warning("Can't get contact for account %s. Don't show notification", accountInfo.id.c_str());
                    return;
                }

                if (g_settings_get_boolean(widgets->settings, "enable-chat-notifications")) {
                    name.erase(std::remove(name.begin(), name.end(), '\r'), name.end());
                    auto body = name + ": " + interaction.body;
                    ring_show_notification(RING_NOTIFIER(widgets->notifier), avatar, uri, name, notifId, _("New message"), body, NotificationType::CHAT);
                }
            }
        }
    } catch (...) {
        g_warning("Can't get account %s", accountId.c_str());
    }
}

void
CppImpl::slotCloseInteraction(const std::string& accountId, const std::string& conversation, uint64_t)
{
    if (!gtk_window_is_active(GTK_WINDOW(self))
        || (chatViewConversation_ && chatViewConversation_->uid != conversation)) {
            return;
    }
    try {
        auto& accountInfo = lrc_->getAccountModel().getAccountInfo(accountId);
        auto notifId = accountInfo.id + ":interaction:" + conversation;
        ring_hide_notification(RING_NOTIFIER(widgets->notifier), notifId);
    } catch (...) {
        g_warning("Can't get account %s", accountId.c_str());
    }
}

void
CppImpl::slotShowIncomingCallView(const std::string& id, lrc::api::conversation::Info origin)
{
    changeAccountSelection(id);

    /* call changeView even if we are already in an incoming call view, since
       the incoming call view holds a copy of the conversation info which has
       the be updated */
    changeView(INCOMING_CALL_VIEW_TYPE, origin);
}

void
CppImpl::slotProfileUpdated(const std::string& id)
{
    auto currentIdx = gtk_combo_box_get_active(GTK_COMBO_BOX(widgets->combobox_account_selector));
    if (currentIdx == -1)
        currentIdx = 0; // If no account selected, select the first account
   refreshAccountSelectorWidget(currentIdx, id);
}

}} // namespace <anonymous>::details

void
ring_main_window_reset(RingMainWindow* self)
{
    g_return_if_fail(IS_RING_MAIN_WINDOW(self));
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(RING_MAIN_WINDOW(self));
    if (priv->cpp->show_settings)
        priv->cpp->leaveSettingsView();
}

//==============================================================================

static void
ring_main_window_init(RingMainWindow *win)
{
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(win);
    gtk_widget_init_template(GTK_WIDGET(win));

    // CppImpl ctor
    priv->cpp = new details::CppImpl {*win};
    priv->notifier = ring_notifier_new();
    priv->cpp->init();
}

static void
ring_main_window_dispose(GObject *object)
{
    auto* self = RING_MAIN_WINDOW(object);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(self);

    delete priv->cpp;
    priv->cpp = nullptr;
    delete priv->notifier;
    priv->notifier = nullptr;

    G_OBJECT_CLASS(ring_main_window_parent_class)->dispose(object);

    if (priv->general_settings_view) {
        g_signal_handler_disconnect(priv->general_settings_view, priv->update_download_folder);
        priv->update_download_folder = 0;
        g_signal_handler_disconnect(priv->notifier, priv->notif_chat_view);
        priv->notif_chat_view = 0;
        g_signal_handler_disconnect(priv->notifier, priv->notif_accept_pending);
        priv->notif_accept_pending = 0;
        g_signal_handler_disconnect(priv->notifier, priv->notif_refuse_pending);
        priv->notif_refuse_pending = 0;
        g_signal_handler_disconnect(priv->notifier, priv->notif_accept_call);
        priv->notif_accept_call = 0;
        g_signal_handler_disconnect(priv->notifier, priv->notif_decline_call);
        priv->notif_decline_call = 0;
    }
}

static void
ring_main_window_finalize(GObject *object)
{
    auto* self = RING_MAIN_WINDOW(object);
    auto* priv = RING_MAIN_WINDOW_GET_PRIVATE(self);

    g_clear_object(&priv->settings);

    G_OBJECT_CLASS(ring_main_window_parent_class)->finalize(object);
}

static void
ring_main_window_class_init(RingMainWindowClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = ring_main_window_finalize;
    G_OBJECT_CLASS(klass)->dispose = ring_main_window_dispose;

    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS (klass),
                                                "/cx/ring/RingGnome/ringmainwindow.ui");

    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, vbox_left_pane);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, notebook_contacts);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, scrolled_window_smartview);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, ring_menu);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, image_ring);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, ring_settings);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, image_settings);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, hbox_settings);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, search_entry);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, stack_main_view);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, vbox_call_view);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, frame_call);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, button_new_conversation  );
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, radiobutton_general_settings);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, radiobutton_media_settings);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, radiobutton_new_account_settings);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, combobox_account_selector);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS (klass), RingMainWindow, scrolled_window_contact_requests);
}

GtkWidget *
ring_main_window_new(GtkApplication *app)
{
    gpointer win = g_object_new(RING_MAIN_WINDOW_TYPE, "application", app, NULL);
    return (GtkWidget *)win;
}
