import shutil
import subprocess
import sys
from pathlib import Path

# Ensure the required library is installed
try:
    from css_html_js_minify import process_single_css_file, process_single_html_file
except ImportError:
    print("Required library 'css_html_js_minify' is not installed.")
    approval = input("Do you want to install it now? (y/n): ").strip().lower()
    if approval in ["yes", "y"]:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "css-html-js-minify"])
        from css_html_js_minify import process_single_css_file, process_single_html_file
    else:
        print("Library installation declined. Exiting.")
        exit(1)

# Prompt the user to select the target destination
print("Select the target destination:")
print("1. ESP8266")
print("2. ESP")
choice = input("Enter 1 or 2: ").strip()

if choice == "1":
    output_dir = Path(__file__).parent / "../esp8266/data/www"
elif choice == "2":
    output_dir = Path(__file__).parent / "../esp/data/www"
else:
    print("Invalid choice. Exiting.")
    exit(1)

# Check if the output directory exists and is not empty
if output_dir.exists() and any(output_dir.iterdir()):
    print(f"The output directory '{output_dir}' is not empty.")
    approval = input("Do you approve clearing it? (y/n): ").strip().lower()
    if approval not in ["yes", "y"]:
        print("Operation canceled by the user.")
        exit(1)
    shutil.rmtree(output_dir)  # Clear the destination directory

# Ensure the output directories exist after cleaning
output_dir.mkdir(parents=True, exist_ok=True)

def minify_file(file_path):
    """Minify a file in place based on its type."""
    if file_path.suffix == ".css":
        process_single_css_file(file_path, overwrite=True)
    elif file_path.suffix == ".html":
        process_single_html_file(file_path, overwrite=True)
    else:
        print(f"Skipping unsupported file: {file_path}")
        return

# Update source directories to include the 'css' subdirectory
html_css_src = Path(__file__).parent / "../web-interface"
css_src = html_css_src / "css"
js_src = html_css_src / "js"

# Copy only .html, .css, and .js files from the source to the destination directory
for file in html_css_src.rglob("*"):
    if file.suffix in [".html", ".css", ".js"]:
        relative_path = file.relative_to(html_css_src)
        destination = output_dir / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, destination)

# Minify HTML and CSS files in the destination directory
for file in output_dir.rglob("*"):
    if file.suffix in [".css", ".html"]:
        minify_file(file)

print("Web files copied and minified successfully.")
print("You can now upload the files to your ESP device using the appropriate tool.")
print("Created by Assad Anabosi")