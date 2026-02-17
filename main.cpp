#include <gtkmm/application.h>
#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/button.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/image.h>
#include <gtkmm/separator.h>
#include <gtkmm/stack.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/overlay.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/textview.h>
#include <gtkmm/textbuffer.h>
#include <glibmm/main.h>
#include <glibmm/dispatcher.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <sstream>
#include <cstdio>
#include <regex>

namespace fs = std::filesystem;

class InstallerWindow : public Gtk::Window {
public:
    InstallerWindow(std::string path) : m_deb_path(path), m_progress_dispatcher(), m_completion_dispatcher(), m_operation_success(false), m_current_progress(0.0) {
        set_title("Software Setup Wizard");
        set_default_size(700, 500);

        m_pkg_name = get_deb_info("Package");
        m_pkg_version = get_deb_info("Version");
        m_pkg_desc = get_deb_info("Description");
        m_is_installed = check_if_installed();
        m_license_text = get_license_content();
        extract_icon_temporary();

        auto main_vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        set_child(*main_vbox);

        auto middle_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        middle_hbox->set_vexpand(true);
        main_vbox->append(*middle_hbox);

        auto sidebar_overlay = Gtk::make_managed<Gtk::Overlay>();
        middle_hbox->append(*sidebar_overlay);

        auto bg_widget = Gtk::make_managed<Gtk::Box>();
        bg_widget->set_size_request(200, -1);
        bg_widget->get_style_context()->add_class("sidebar-filled-bg");
        sidebar_overlay->set_child(*bg_widget);

        m_sidebar_box.set_orientation(Gtk::Orientation::VERTICAL);
        if (!m_extracted_icon_path.empty() && fs::exists(m_extracted_icon_path)) {
            m_sidebar_img.set(m_extracted_icon_path);
        } else {
            m_sidebar_img.set_from_icon_name("system-software-install");
        }
        m_sidebar_img.set_pixel_size(110);
        m_sidebar_img.set_margin(45);
        m_sidebar_box.append(m_sidebar_img);
        sidebar_overlay->add_overlay(m_sidebar_box);

        m_stack.set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);
        m_stack.set_hexpand(true);
        middle_hbox->append(m_stack);

        create_welcome_page();
        if (!m_license_text.empty()) create_license_page();
        create_install_page();

        auto footer_sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
        main_vbox->append(*footer_sep);

        m_button_box.set_spacing(10);
        m_button_box.set_margin(15);
        m_button_box.set_halign(Gtk::Align::END);

        m_btn_back.set_label("Back");
        m_btn_back.set_sensitive(false);
        m_btn_back.set_visible(false);
        m_btn_back.signal_clicked().connect(sigc::mem_fun(*this, &InstallerWindow::on_back_clicked));

        m_btn_next.set_label(m_license_text.empty() ? (m_is_installed ? "Reinstall" : "Install") : "Next");
        m_btn_next.signal_clicked().connect(sigc::mem_fun(*this, &InstallerWindow::on_next_clicked));

        m_btn_uninstall.set_label("Uninstall");
        m_btn_uninstall.set_visible(m_is_installed);
        m_btn_uninstall.signal_clicked().connect(sigc::mem_fun(*this, &InstallerWindow::on_uninstall_clicked));

        m_btn_cancel.set_label("Cancel");
        m_btn_cancel.signal_clicked().connect([this](){ hide(); });

        m_button_box.append(m_btn_uninstall);
        m_button_box.append(m_btn_back);
        m_button_box.append(m_btn_next);
        m_button_box.append(m_btn_cancel);
        main_vbox->append(m_button_box);

        m_progress_dispatcher.connect(sigc::mem_fun(*this, &InstallerWindow::update_progress_from_thread));
        m_completion_dispatcher.connect(sigc::mem_fun(*this, &InstallerWindow::update_completion_from_thread));

        apply_custom_style();
    }

    ~InstallerWindow() {
        if (m_install_thread.joinable()) {
            m_install_thread.join();
        }
        if (!m_extracted_icon_path.empty()) fs::remove(m_extracted_icon_path);
    }

private:
    void create_welcome_page() {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        box->set_margin(30); box->set_spacing(15);
        auto title = Gtk::make_managed<Gtk::Label>(m_pkg_name);
        title->get_style_context()->add_class("large-title");
        title->set_halign(Gtk::Align::START);
        auto ver = Gtk::make_managed<Gtk::Label>("Version: " + m_pkg_version);
        ver->set_halign(Gtk::Align::START);
        auto desc = Gtk::make_managed<Gtk::Label>(m_pkg_desc);
        desc->set_wrap(true); desc->set_halign(Gtk::Align::START);
        box->append(*title); box->append(*ver); box->append(*desc);
        m_stack.add(*box, "welcome");
    }

    void create_license_page() {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        box->set_margin(30); box->set_spacing(10);
        auto l_title = Gtk::make_managed<Gtk::Label>("License Agreement");
        l_title->get_style_context()->add_class("large-title");
        l_title->set_halign(Gtk::Align::START);
        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_vexpand(true);
        scroll->get_style_context()->add_class("license-area");
        auto content = Gtk::make_managed<Gtk::Label>(m_license_text);
        content->set_margin(10); content->set_wrap(true);
        scroll->set_child(*content);
        m_check_accept.set_label("I accept the terms in the License Agreement");
        m_check_accept.signal_toggled().connect([this](){ m_btn_next.set_sensitive(m_check_accept.get_active()); });
        box->append(*l_title); box->append(*scroll); box->append(m_check_accept);
        m_stack.add(*box, "license");
    }

    void create_install_page() {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        box->set_margin(30);
        box->set_vexpand(true);
        box->set_spacing(15);
        m_progress_label.set_text("Waiting for authorization...");
        box->append(m_progress_label);
        box->append(m_progress_bar);
        m_error_scroll.set_vexpand(true);
        m_error_scroll.set_visible(false);
        m_error_text.set_editable(false);
        m_error_text.set_monospace(true);
        m_error_text.set_margin(10);
        m_error_scroll.set_child(m_error_text);
        box->append(m_error_scroll);
        m_stack.add(*box, "progress");
    }

    void on_next_clicked() {
        std::string current = m_stack.get_visible_child_name();
        if (current == "welcome") {
            if (!m_license_text.empty()) {
                m_stack.set_visible_child("license");
                m_btn_next.set_label("Next");
                m_btn_next.set_sensitive(m_check_accept.get_active());
                m_btn_back.set_sensitive(true);
                m_btn_back.set_visible(true);

                
            } else { start_install_sequence(); }
        } else if (current == "license") {
            start_install_sequence();
        } else if (current == "progress") {
            if (m_btn_next.get_label() == "Finish") hide();
        }
    }

    void start_install_sequence() {
        m_stack.set_visible_child("progress");
        m_btn_next.set_sensitive(false);
        m_btn_back.set_sensitive(false);
        m_btn_uninstall.set_sensitive(false);
        m_progress_label.set_text("Authenticating...");
        m_progress_bar.set_fraction(0.0);
        m_error_scroll.set_visible(false);
        m_command_output.clear();
        m_current_progress = 0.0;
        m_progress_status = "Authenticating...";
        m_btn_cancel.set_visible(false);
        m_btn_next.set_label("Finish");

        if (m_install_thread.joinable()) {
            m_install_thread.join();
        }
        m_install_thread = std::thread(&InstallerWindow::run_install_thread, this);
    }

    void run_install_thread() {
        std::string abs_path = fs::absolute(m_deb_path).string();
        std::string cmd = "pkexec apt-get install -y --reinstall " + abs_path + " 2>&1";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            {
                std::lock_guard<std::mutex> lock(m_output_mutex);
                m_operation_success = false;
                m_command_output = "Failed to start installation process.";
            }
            m_completion_dispatcher.emit();
            return;
        }

        char buffer[1024];
        std::string line;
        double last_progress = -1.0;
        std::regex progress_regex(R"(\[?\s*(\d+)%\s*\]?)");

        while (fgets(buffer, sizeof(buffer), pipe)) {
            line = buffer;
            {
                std::lock_guard<std::mutex> lock(m_output_mutex);
                m_command_output += line;
            }

            std::smatch match;
            if (std::regex_search(line, match, progress_regex)) {
                double progress = std::stod(match[1].str()) / 100.0;
                if (progress - last_progress >= 0.01 || progress == 1.0) {
                    last_progress = progress;
                    {
                        std::lock_guard<std::mutex> lock(m_output_mutex);
                        m_current_progress = progress;
                        if (line.find("Reading") != std::string::npos) {
                            m_progress_status = "Reading package lists...";
                        } else if (line.find("Unpacking") != std::string::npos) {
                            m_progress_status = "Unpacking packages...";
                        } else if (line.find("Setting up") != std::string::npos) {
                            m_progress_status = "Setting up packages...";
                        } else if (line.find("Processing") != std::string::npos) {
                            m_progress_status = "Processing triggers...";
                        } else {
                            m_progress_status = "Installing...";
                        }
                    }
                    m_progress_dispatcher.emit();
                }
            } else if (line.find("Reading") != std::string::npos || line.find("Preparing") != std::string::npos) {
                {
                    std::lock_guard<std::mutex> lock(m_output_mutex);
                    m_progress_status = "Preparing installation...";
                }
                m_progress_dispatcher.emit();
            }
        }

        int exit_code = pclose(pipe);
        {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_operation_success = (exit_code == 0);
        }
        m_completion_dispatcher.emit();
    }

    void on_uninstall_clicked() {
        m_stack.set_visible_child("progress");
        m_btn_next.set_sensitive(false);
        m_btn_back.set_sensitive(false);
        m_btn_cancel.set_sensitive(false);
        m_progress_label.set_text("Authenticating removal...");
        m_progress_bar.set_fraction(0.0);
        m_error_scroll.set_visible(false);
        m_command_output.clear();
        m_current_progress = 0.0;
        m_progress_status = "Authenticating removal...";
        m_btn_uninstall.set_visible(false);
        m_btn_cancel.set_visible(false);
        m_btn_next.set_label("Finish");

        
        if (m_install_thread.joinable()) {
            m_install_thread.join();
        }
        m_install_thread = std::thread(&InstallerWindow::run_uninstall_thread, this);
    }

    void run_uninstall_thread() {
        std::string cmd = "pkexec apt-get remove -y " + m_pkg_name + " 2>&1";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            {
                std::lock_guard<std::mutex> lock(m_output_mutex);
                m_operation_success = false;
                m_command_output = "Failed to start removal process.";
            }
            m_completion_dispatcher.emit();
            return;
        }

        char buffer[1024];
        std::string line;
        double last_progress = -1.0;
        std::regex progress_regex(R"(\[?\s*(\d+)%\s*\]?)");

        while (fgets(buffer, sizeof(buffer), pipe)) {
            line = buffer;
            {
                std::lock_guard<std::mutex> lock(m_output_mutex);
                m_command_output += line;
            }

            std::smatch match;
            if (std::regex_search(line, match, progress_regex)) {
                double progress = std::stod(match[1].str()) / 100.0;
                if (progress - last_progress >= 0.01 || progress == 1.0) {
                    last_progress = progress;
                    {
                        std::lock_guard<std::mutex> lock(m_output_mutex);
                        m_current_progress = progress;
                        if (line.find("Reading") != std::string::npos) {
                            m_progress_status = "Reading package lists...";
                        } else if (line.find("Removing") != std::string::npos) {
                            m_progress_status = "Removing packages...";
                        } else if (line.find("Processing") != std::string::npos) {
                            m_progress_status = "Processing triggers...";
                        } else {
                            m_progress_status = "Uninstalling...";
                        }
                    }
                    m_progress_dispatcher.emit();
                }
            } else if (line.find("Reading") != std::string::npos || line.find("Preparing") != std::string::npos) {
                {
                    std::lock_guard<std::mutex> lock(m_output_mutex);
                    m_progress_status = "Preparing removal...";
                }
                m_progress_dispatcher.emit();
            }
        }

        int exit_code = pclose(pipe);
        {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_operation_success = (exit_code == 0);
            if (m_operation_success) {
                m_progress_status = "Successfully removed.";
            } else {
                m_progress_status = "Removal failed.";
            }
        }
        m_completion_dispatcher.emit();
    }

    void update_progress_from_thread() {
        std::lock_guard<std::mutex> lock(m_output_mutex);
        m_progress_bar.set_fraction(m_current_progress);
        if (!m_progress_status.empty()) {
            m_progress_label.set_text(m_progress_status);
        }
    }

    void update_completion_from_thread() {
        std::lock_guard<std::mutex> lock(m_output_mutex);
        if (m_operation_success) {
            m_progress_label.set_text(m_progress_status.empty() ? "Operation Complete!" : m_progress_status);
            m_progress_status = "Operation Complete!";
            m_progress_bar.set_fraction(1.0);
            m_btn_next.set_label("Finish");
            m_btn_next.set_visible(true);
            m_btn_next.set_sensitive(true);
        } else {
            m_progress_label.set_text(m_progress_status.empty() ? "Operation cancelled or failed." : m_progress_status);
            auto buffer = m_error_text.get_buffer();
            buffer->set_text(m_command_output);
            m_error_scroll.set_visible(true);
            m_btn_cancel.set_sensitive(true);
        }
        m_btn_cancel.set_visible(false);
        m_btn_back.set_visible(false);
        m_btn_uninstall.set_visible(false);
    }

    void on_back_clicked() {
        std::string current = m_stack.get_visible_child_name();
        if (current == "license") {
            m_stack.set_visible_child("welcome");
            m_btn_next.set_label(m_license_text.empty() ? "Install" : "Next");
            m_btn_next.set_sensitive(true);
            m_btn_back.set_sensitive(false);
            m_btn_back.set_visible(false);
        }
    }

    void apply_custom_style() {
        auto css = Gtk::CssProvider::create();
        css->load_from_data(".sidebar-filled-bg { background-color: #3584e4; } .large-title { font-weight: bold; font-size: 1.8em; } .license-area { background: white; color: black; font-family: monospace; border: 1px solid #ccc; }");
        get_style_context()->add_provider_for_display(get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    void extract_icon_temporary() {
        std::string find_cmd = "dpkg-deb --contents " + m_deb_path + " | grep -i '128x128.*\\.png' | head -n 1 | awk '{print $6}'";
        FILE* pipe = popen(find_cmd.c_str(), "r");
        char buffer[512]; std::string internal_path;
        if (pipe && fgets(buffer, sizeof(buffer), pipe)) internal_path = buffer;
        if (pipe) pclose(pipe);
        if (!internal_path.empty()) {
            internal_path.erase(internal_path.find_last_not_of(" \n\r\t") + 1);
            if(internal_path[0] == '.') internal_path.erase(0, 1);
            m_extracted_icon_path = "/tmp/" + m_pkg_name + "_icon.png";
            std::system(("dpkg-deb --fsys-tarfile " + m_deb_path + " | tar -xOf - ." + internal_path + " > " + m_extracted_icon_path).c_str());
        }
    }

    std::string get_license_content() {
        std::string cmd = "dpkg-deb --fsys-tarfile " + m_deb_path + " | tar -xOf - ./usr/share/doc/" + m_pkg_name + "/copyright 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        char buffer[1024]; std::string res;
        while (pipe && fgets(buffer, sizeof(buffer), pipe)) res += buffer;
        if(pipe) pclose(pipe);
        return res;
    }

    std::string get_deb_info(std::string field) {
        std::string cmd = "dpkg-deb -f " + m_deb_path + " " + field + " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        char buffer[512]; std::string res;
        while (pipe && fgets(buffer, sizeof(buffer), pipe)) res += buffer;
        if(pipe) pclose(pipe);
        if(!res.empty()) res.erase(res.find_last_not_of(" \n\r\t") + 1);
        return res;
    }

    bool check_if_installed() { return (std::system(("dpkg -l " + m_pkg_name + " 2>/dev/null | grep '^ii' > /dev/null").c_str()) == 0); }

    std::string m_deb_path, m_pkg_name, m_pkg_version, m_pkg_desc, m_license_text, m_extracted_icon_path;
    bool m_is_installed;
    Gtk::Box m_sidebar_box, m_button_box;
    Gtk::Stack m_stack;
    Gtk::Button m_btn_next, m_btn_back, m_btn_cancel, m_btn_uninstall;
    Gtk::ProgressBar m_progress_bar;
    Gtk::Label m_progress_label;
    Gtk::Image m_sidebar_img;
    Gtk::CheckButton m_check_accept;
    Glib::Dispatcher m_progress_dispatcher;
    Glib::Dispatcher m_completion_dispatcher;
    std::thread m_install_thread;
    std::mutex m_output_mutex;
    std::string m_command_output;
    Gtk::TextView m_error_text;
    Gtk::ScrolledWindow m_error_scroll;
    bool m_operation_success;
    double m_current_progress;
    std::string m_progress_status;
};

class WizardApp : public Gtk::Application {
protected:
    WizardApp() : Gtk::Application("com.wizard.installer", Gio::Application::Flags::HANDLES_OPEN) {}
public:
    static Glib::RefPtr<WizardApp> create() { return Glib::RefPtr<WizardApp>(new WizardApp()); }
protected:
    void on_open(const type_vec_files& files, const Glib::ustring&) override {
        auto win = new InstallerWindow(files[0]->get_path());
        add_window(*win); win->set_visible(true);
    }
    void on_activate() override {}
};

int main(int argc, char* argv[]) { return WizardApp::create()->run(argc, argv); }
