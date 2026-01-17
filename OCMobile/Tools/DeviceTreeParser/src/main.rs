use std::env;
use std::fs::{self, File, OpenOptions, Read, Write};
use std::io::{BufReader, BufWriter, Write as IoWrite};
use clap::Parser;
use std::path::{Path, PathBuf};
use std::collections::HashMap;

#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct Args {
    #[clap(short, long, value_parser)]
    tree: String,

    #[clap(long, value_parser)]
    export_plist: Option<String>,
}

#[derive(Debug)]
struct HardwareReport {
    device_info: HashMap<String, String>,
    key_files: HashMap<String, bool>,
    key_dirs: HashMap<String, bool>,
    drivers: HashMap<String, Vec<String>>,
    structure_valid: bool,
}

fn detect_android_device_tree_structure(tree_path: &str, export_plist: Option<String>) {
    let path = Path::new(tree_path);

    if !path.exists() {
        eprintln!("Error: Path '{}' does not exist", tree_path);
        return;
    }

    if !path.is_dir() {
        eprintln!("Error: Path '{}' is not a directory", tree_path);
        return;
    }

    println!("Analyzing Android device tree at: {}\n", tree_path);

    // Common Android device tree files and directories
    let key_files = vec![
        "AndroidProducts.mk",
        "BoardConfig.mk",
        "device.mk",
        "system.prop",
        "vendorsetup.sh",
        "extract-files.sh",
        "setup-makefiles.sh",
    ];

    let key_dirs = vec![
        "overlay",
        "proprietary",
        "proprietary-files.txt",
        "configs",
        "rootdir",
        "recovery",
        "prebuilt",
    ];

    let mut found_files: HashMap<String, PathBuf> = HashMap::new();
    let mut found_dirs: HashMap<String, PathBuf> = HashMap::new();
    let mut files_status: HashMap<String, bool> = HashMap::new();
    let mut dirs_status: HashMap<String, bool> = HashMap::new();

    // Scan the tree directory
    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            let entry_path = entry.path();
            let entry_name = entry.file_name().to_string_lossy().to_string();

            if entry_path.is_file() && key_files.contains(&entry_name.as_str()) {
                found_files.insert(entry_name.clone(), entry_path);
                files_status.insert(entry_name, true);
            } else if entry_path.is_dir() && key_dirs.contains(&entry_name.as_str()) {
                found_dirs.insert(entry_name.clone(), entry_path);
                dirs_status.insert(entry_name, true);
            }
        }
    }

    // Mark missing files/dirs
    for file in &key_files {
        files_status.entry(file.to_string()).or_insert(false);
    }
    for dir in &key_dirs {
        dirs_status.entry(dir.to_string()).or_insert(false);
    }

    // Detect device info from path or files
    let device_info = extract_device_info(path, &found_files).unwrap_or_else(HashMap::new);

    // Print results
    println!("=== Device Tree Structure Detection ===\n");

    if !device_info.is_empty() {
        println!("Device Information:");
        if let Some(vendor) = device_info.get("vendor") {
            println!("  Vendor: {}", vendor);
        }
        if let Some(device) = device_info.get("device") {
            println!("  Device: {}", device);
        }
        println!();
    }

    println!("Key Files Found ({}/{}):", found_files.len(), key_files.len());
    for file in &key_files {
        if found_files.contains_key(*file) {
            println!("  ✓ {}", file);
        } else {
            println!("  ✗ {} (missing)", file);
        }
    }

    println!("\nKey Directories Found ({}/{}):", found_dirs.len(), key_dirs.len());
    for dir in &key_dirs {
        if found_dirs.contains_key(*dir) {
            println!("  ✓ {}", dir);
        } else {
            println!("  ✗ {} (missing)", dir);
        }
    }

    // Analyze structure validity
    println!("\n=== Structure Analysis ===");
    let has_makefile = found_files.contains_key("AndroidProducts.mk")
        || found_files.contains_key("device.mk");
    let has_board_config = found_files.contains_key("BoardConfig.mk");
    let structure_valid = has_makefile && has_board_config;

    if structure_valid {
        println!("Status: ✓ Valid Android device tree structure detected");
    } else if has_makefile || has_board_config {
        println!("Status: ⚠ Partial device tree structure (missing critical files)");
    } else {
        println!("Status: ✗ Does not appear to be a valid Android device tree");
    }

    // Parse and list device drivers
    println!("\n=== Device Drivers ===");
    let drivers = list_device_drivers(path);

    // Export to plist if requested
    if let Some(plist_path) = export_plist {
        let report = HardwareReport {
            device_info,
            key_files: files_status,
            key_dirs: dirs_status,
            drivers,
            structure_valid,
        };

        match export_to_plist(&report, &plist_path) {
            Ok(_) => println!("\n✓ Hardware report exported to: {}", plist_path),
            Err(e) => eprintln!("\n✗ Failed to export plist: {}", e),
        }
    }
}

fn list_device_drivers(tree_path: &Path) -> HashMap<String, Vec<String>> {
    let mut drivers = HashMap::new();

    // Scan for .dts and .dtsi files (Device Tree Source files)
    scan_for_device_tree_sources(tree_path, &mut drivers);

    // Parse BoardConfig.mk for kernel modules and drivers
    let board_config_path = tree_path.join("BoardConfig.mk");
    if board_config_path.exists() {
        parse_board_config(&board_config_path, &mut drivers);
    }

    // Parse device.mk for HAL and driver configurations
    let device_mk_path = tree_path.join("device.mk");
    if device_mk_path.exists() {
        parse_device_mk(&device_mk_path, &mut drivers);
    }

    // Look for prebuilt drivers in various locations
    scan_prebuilt_modules(tree_path, &mut drivers);

    if drivers.is_empty() {
        println!("No device drivers found in the tree.");
    } else {
        // Categorize and display drivers
        display_drivers_by_category(&drivers);
    }

    drivers
}

fn scan_for_device_tree_sources(path: &Path, drivers: &mut HashMap<String, Vec<String>>) {
    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            let entry_path = entry.path();

            if entry_path.is_file() {
                let file_name = entry_path.file_name().unwrap().to_string_lossy();
                if file_name.ends_with(".dts") || file_name.ends_with(".dtsi") {
                    parse_dts_file(&entry_path, drivers);
                }
            } else if entry_path.is_dir() {
                // Recursively scan subdirectories
                scan_for_device_tree_sources(&entry_path, drivers);
            }
        }
    }
}

fn parse_dts_file(dts_path: &Path, drivers: &mut HashMap<String, Vec<String>>) {
    if let Ok(content) = fs::read_to_string(dts_path) {
        let file_name = dts_path.file_name().unwrap().to_string_lossy().to_string();

        // Look for compatible strings which indicate driver bindings
        for line in content.lines() {
            let trimmed = line.trim();
            if trimmed.starts_with("compatible") {
                // Extract compatible string: compatible = "vendor,device";
                if let Some(compat_str) = extract_compatible_string(trimmed) {
                    drivers.entry("Device Tree Bindings".to_string())
                        .or_insert_with(Vec::new)
                        .push(format!("{} ({})", compat_str, file_name));
                }
            }
        }
    }
}

fn extract_compatible_string(line: &str) -> Option<String> {
    // Parse: compatible = "qcom,msm8996", "qcom,somename";
    if let Some(start) = line.find('"') {
        if let Some(end) = line[start + 1..].find('"') {
            return Some(line[start + 1..start + 1 + end].to_string());
        }
    }
    None
}

fn parse_board_config(board_config_path: &Path, drivers: &mut HashMap<String, Vec<String>>) {
    if let Ok(content) = fs::read_to_string(board_config_path) {
        for line in content.lines() {
            let trimmed = line.trim();

            // Look for kernel module definitions
            if trimmed.contains("BOARD_VENDOR_KERNEL_MODULES") ||
               trimmed.contains("KERNEL_MODULES") {
                if let Some(modules) = extract_kernel_modules(trimmed) {
                    for module in modules {
                        drivers.entry("Kernel Modules".to_string())
                            .or_insert_with(Vec::new)
                            .push(module);
                    }
                }
            }

            // Look for WiFi driver
            if trimmed.starts_with("BOARD_WLAN_DEVICE") || trimmed.starts_with("WPA_SUPPLICANT_VERSION") {
                if let Some(value) = extract_makefile_value(trimmed) {
                    drivers.entry("WiFi Driver".to_string())
                        .or_insert_with(Vec::new)
                        .push(value);
                }
            }

            // Look for Bluetooth
            if trimmed.starts_with("BOARD_HAVE_BLUETOOTH") || trimmed.starts_with("BOARD_BLUETOOTH_BDROID_BUILDCFG") {
                if let Some(value) = extract_makefile_value(trimmed) {
                    drivers.entry("Bluetooth Driver".to_string())
                        .or_insert_with(Vec::new)
                        .push(value);
                }
            }

            // Look for GPU/Graphics
            if trimmed.starts_with("TARGET_BOARD_PLATFORM") {
                if let Some(value) = extract_makefile_value(trimmed) {
                    drivers.entry("GPU/Platform".to_string())
                        .or_insert_with(Vec::new)
                        .push(value);
                }
            }
        }
    }
}

fn parse_device_mk(device_mk_path: &Path, drivers: &mut HashMap<String, Vec<String>>) {
    if let Ok(content) = fs::read_to_string(device_mk_path) {
        for line in content.lines() {
            let trimmed = line.trim();

            // Look for HAL packages (Hardware Abstraction Layer)
            if trimmed.contains("PRODUCT_PACKAGES") {
                if trimmed.contains("android.hardware.") {
                    if let Some(hal) = extract_hal_name(trimmed) {
                        drivers.entry("HAL (Hardware Abstraction Layer)".to_string())
                            .or_insert_with(Vec::new)
                            .push(hal);
                    }
                }
            }

            // Look for audio HAL
            if trimmed.contains("audio.") || trimmed.contains("AUDIO_") {
                if let Some(value) = extract_makefile_value(trimmed) {
                    drivers.entry("Audio Driver".to_string())
                        .or_insert_with(Vec::new)
                        .push(value);
                }
            }

            // Look for camera HAL
            if trimmed.contains("camera.") || trimmed.contains("CAMERA_") {
                if let Some(value) = extract_makefile_value(trimmed) {
                    drivers.entry("Camera Driver".to_string())
                        .or_insert_with(Vec::new)
                        .push(value);
                }
            }
        }
    }
}

fn scan_prebuilt_modules(tree_path: &Path, drivers: &mut HashMap<String, Vec<String>>) {
    let prebuilt_paths = vec![
        tree_path.join("prebuilt"),
        tree_path.join("proprietary"),
        tree_path.join("vendor"),
    ];

    for prebuilt_path in prebuilt_paths {
        if prebuilt_path.exists() {
            scan_for_ko_files(&prebuilt_path, drivers);
        }
    }
}

fn scan_for_ko_files(path: &Path, drivers: &mut HashMap<String, Vec<String>>) {
    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            let entry_path = entry.path();

            if entry_path.is_file() {
                let file_name = entry_path.file_name().unwrap().to_string_lossy();
                if file_name.ends_with(".ko") {
                    // .ko files are compiled kernel modules
                    drivers.entry("Prebuilt Kernel Modules".to_string())
                        .or_insert_with(Vec::new)
                        .push(file_name.to_string());
                }
            } else if entry_path.is_dir() {
                scan_for_ko_files(&entry_path, drivers);
            }
        }
    }
}

fn extract_kernel_modules(line: &str) -> Option<Vec<String>> {
    // Extract module names from lines like: BOARD_VENDOR_KERNEL_MODULES := module1.ko module2.ko
    if let Some(pos) = line.find(":=") {
        let modules_str = line[pos + 2..].trim();
        let modules: Vec<String> = modules_str
            .split_whitespace()
            .filter(|s| !s.is_empty() && s.ends_with(".ko"))
            .map(|s| s.to_string())
            .collect();
        if !modules.is_empty() {
            return Some(modules);
        }
    }
    None
}

fn extract_makefile_value(line: &str) -> Option<String> {
    // Extract value from VAR := value or VAR = value
    if let Some(pos) = line.find(":=").or_else(|| line.find('=')) {
        let value = line[pos + if line.chars().nth(pos + 1) == Some('=') { 2 } else { 1 }..]
            .trim()
            .to_string();
        if !value.is_empty() {
            return Some(value);
        }
    }
    None
}

fn extract_hal_name(line: &str) -> Option<String> {
    // Extract HAL names like android.hardware.audio@2.0-impl
    if let Some(start) = line.find("android.hardware.") {
        let substring = &line[start..];
        if let Some(end) = substring.find(char::is_whitespace).or_else(|| Some(substring.len())) {
            return Some(substring[..end].trim().to_string());
        }
    }
    None
}

fn display_drivers_by_category(drivers: &HashMap<String, Vec<String>>) {
    let mut categories: Vec<_> = drivers.keys().collect();
    categories.sort();

    for category in categories {
        if let Some(driver_list) = drivers.get(category) {
            println!("\n{}:", category);
            let mut unique_drivers: Vec<_> = driver_list.iter().collect();
            unique_drivers.sort();
            unique_drivers.dedup();

            for driver in unique_drivers {
                println!("  • {}", driver);
            }
        }
    }

    println!("\nTotal driver categories: {}", drivers.len());
}

fn export_to_plist(report: &HardwareReport, plist_path: &str) -> std::io::Result<()> {
    let mut file = File::create(plist_path)?;

    // Write plist header
    writeln!(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>")?;
    writeln!(file, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">")?;
    writeln!(file, "<plist version=\"1.0\">")?;
    writeln!(file, "<dict>")?;

    // Device Information
    writeln!(file, "\t<key>DeviceInformation</key>")?;
    writeln!(file, "\t<dict>")?;
    for (key, value) in &report.device_info {
        writeln!(file, "\t\t<key>{}</key>", escape_xml(key))?;
        writeln!(file, "\t\t<string>{}</string>", escape_xml(value))?;
    }
    writeln!(file, "\t</dict>")?;

    // Structure Validity
    writeln!(file, "\t<key>StructureValid</key>")?;
    writeln!(file, "\t<{} />", if report.structure_valid { "true" } else { "false" })?;

    // Key Files
    writeln!(file, "\t<key>KeyFiles</key>")?;
    writeln!(file, "\t<dict>")?;
    let mut files: Vec<_> = report.key_files.iter().collect();
    files.sort_by_key(|(k, _)| *k);
    for (file_name, found) in files {
        writeln!(file, "\t\t<key>{}</key>", escape_xml(file_name))?;
        writeln!(file, "\t\t<{} />", if *found { "true" } else { "false" })?;
    }
    writeln!(file, "\t</dict>")?;

    // Key Directories
    writeln!(file, "\t<key>KeyDirectories</key>")?;
    writeln!(file, "\t<dict>")?;
    let mut dirs: Vec<_> = report.key_dirs.iter().collect();
    dirs.sort_by_key(|(k, _)| *k);
    for (dir_name, found) in dirs {
        writeln!(file, "\t\t<key>{}</key>", escape_xml(dir_name))?;
        writeln!(file, "\t\t<{} />", if *found { "true" } else { "false" })?;
    }
    writeln!(file, "\t</dict>")?;

    // Device Drivers
    writeln!(file, "\t<key>DeviceDrivers</key>")?;
    writeln!(file, "\t<dict>")?;
    let mut categories: Vec<_> = report.drivers.keys().collect();
    categories.sort();
    for category in categories {
        if let Some(driver_list) = report.drivers.get(category) {
            writeln!(file, "\t\t<key>{}</key>", escape_xml(category))?;
            writeln!(file, "\t\t<array>")?;
            let mut unique_drivers: Vec<_> = driver_list.iter().collect();
            unique_drivers.sort();
            unique_drivers.dedup();
            for driver in unique_drivers {
                writeln!(file, "\t\t\t<string>{}</string>", escape_xml(driver))?;
            }
            writeln!(file, "\t\t</array>")?;
        }
    }
    writeln!(file, "\t</dict>")?;

    // Close plist
    writeln!(file, "</dict>")?;
    writeln!(file, "</plist>")?;

    Ok(())
}

fn escape_xml(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&apos;")
}

fn extract_device_info(path: &Path, found_files: &HashMap<String, PathBuf>) -> Option<HashMap<String, String>> {
    let mut info = HashMap::new();

    // Try to extract from path (common format: vendor/manufacturer/device)
    let path_str = path.to_string_lossy();
    let parts: Vec<&str> = path_str.split('/').collect();

    if parts.len() >= 2 {
        info.insert("vendor".to_string(), parts[parts.len() - 2].to_string());
        info.insert("device".to_string(), parts[parts.len() - 1].to_string());
    }

    // Try to parse from AndroidProducts.mk or device.mk
    if let Some(android_products) = found_files.get("AndroidProducts.mk") {
        if let Ok(content) = fs::read_to_string(android_products) {
            for line in content.lines() {
                if line.contains("PRODUCT_NAME") {
                    info.insert("product_name".to_string(),
                        line.split('=').nth(1).unwrap_or("").trim().to_string());
                }
            }
        }
    }

    if info.is_empty() {
        None
    } else {
        Some(info)
    }
}

fn main() {
    let args = Args::parse();
    detect_android_device_tree_structure(&args.tree, args.export_plist);
}
