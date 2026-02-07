# === Emily's Management Console v1.0 ===
# Functions: Save Slot Management, File Uploads, Image Extraction

import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, simpledialog
import requests
import json
import os
import datetime
from PIL import Image, ImageTk
from io import BytesIO

# --- USER CONFIGURATION ---
# The IP Address of the EmilyBrain unit
EMILY_BRAIN_IP = "192.168.68.201" 
EMILY_BRAIN_PORT = 80

# Your Venice.ai API Key (for the Image Gallery feature)
VENICE_API_KEY = "YOUR_VENICE_API_KEY_HERE"

# --- SYSTEM CONSTANTS ---
PERSONA_DIR = "personas"
# ------------------------

class ImageGalleryApp:
    """Sub-window for extracting and generating images from chat history."""
    def __init__(self, parent):
        self.window = tk.Toplevel(parent)
        self.window.title("Emily's Art Gallery")
        self.window.geometry("900x650")

        # --- Frames ---
        main_frame = tk.Frame(self.window, padx=10, pady=10)
        main_frame.pack(fill="both", expand=True)

        left_frame = tk.Frame(main_frame)
        left_frame.pack(side="left", fill="y", padx=(0, 10))

        self.right_frame = tk.Frame(main_frame)
        self.right_frame.pack(side="left", fill="both", expand=True)

        # --- Left Panel: File & Model Selection ---
        tk.Button(left_frame, text="Load Chat History (.jsonl)...", command=self.load_history, height=2).pack(fill="x", pady=(0,10))
        
        # Model Selection
        model_frame = tk.Frame(left_frame)
        model_frame.pack(fill="x", pady=(5, 0))
        tk.Label(model_frame, text="Select Image Model:").pack(anchor="w")
        
        self.models = ["venice-sd35", "wai-Illustrious", "hidream", "lustify-sdxl", "qwen-image", "lustify-v7"]
        self.selected_model = tk.StringVar(self.window)
        self.selected_model.set(self.models[0]) 
        
        model_menu = tk.OptionMenu(model_frame, self.selected_model, *self.models)
        model_menu.pack(fill="x")

        # Prompt List
        tk.Label(left_frame, text="Found Prompts:").pack(anchor="w", pady=(10,0))
        self.prompt_listbox = tk.Listbox(left_frame, width=45, height=25)
        self.prompt_listbox.pack(pady=5, fill="y", expand=True)
        self.prompt_listbox.bind('<<ListboxSelect>>', self.on_prompt_select)

        self.prompts = [] 

        # --- Right Panel: Preview & Actions ---
        self.image_label = tk.Label(self.right_frame, text="Select a prompt to generate an image.", bg="#333", fg="#EEE")
        self.image_label.pack(fill="both", expand=True)

        action_frame = tk.Frame(self.right_frame)
        action_frame.pack(fill="x", pady=10)

        self.generate_button = tk.Button(action_frame, text="Generate Image", command=self.generate_image, state="disabled", bg="#007bff", fg="white", font=("Arial", 10, "bold"))
        self.generate_button.pack(side="left", expand=True, fill="x", padx=(0, 5))

        self.save_button = tk.Button(action_frame, text="Save Image As...", command=self.save_image, state="disabled")
        self.save_button.pack(side="left", expand=True, fill="x")
        
        self.current_image_data = None

    def load_history(self):
        """Loads a .jsonl file and extracts prompts."""
        file_path = filedialog.askopenfilename(
            title="Select chat_history.jsonl",
            filetypes=[("JSON Lines", "*.jsonl"), ("All files", "*.*")]
        )
        if not file_path: return

        self.prompt_listbox.delete(0, tk.END)
        self.prompts = []
        
        count = 0
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                for line in f:
                    try:
                        log_entry = json.loads(line)
                        if log_entry.get("role") == "assistant" and "tool_calls" in log_entry:
                            for tool_call in log_entry["tool_calls"]:
                                args = json.loads(tool_call["function"]["arguments"])
                                # Support both parameter names (old & new)
                                prompt = args.get("prompt") or args.get("visual_prompt") or args.get("begeleidende_afbeelding_prompt")
                                
                                if prompt:
                                    if prompt not in self.prompts:
                                        self.prompts.append(prompt)
                                        display_text = (prompt[:40] + '...') if len(prompt) > 40 else prompt
                                        self.prompt_listbox.insert(tk.END, display_text)
                                        count += 1
                    except json.JSONDecodeError:
                        continue 
            messagebox.showinfo("Success", f"Found {count} image prompts!")
        except Exception as e:
            messagebox.showerror("Error", f"Could not read file: {e}")

    def on_prompt_select(self, event):
        self.generate_button.config(state="normal")
        self.save_button.config(state="disabled")
        self.current_image_data = None

    def generate_image(self):
        selected_indices = self.prompt_listbox.curselection()
        if not selected_indices: return

        full_prompt = self.prompts[selected_indices[0]]
        selected_model_name = self.selected_model.get()
        
        self.image_label.config(text="Generating... Please wait...", image='')
        self.window.update_idletasks()

        try:
            url = "https://api.venice.ai/api/v1/image/generate"
            payload = {
                "model": selected_model_name,
                "safe_mode": False,
                "prompt": full_prompt,
                "width": 512,
                "height": 512,
                "format": "jpeg",
                "return_binary": True
            }
            headers = {
                "Authorization": f"Bearer {VENICE_API_KEY}",
                "Content-Type": "application/json"
            }
            response = requests.post(url, json=payload, headers=headers, timeout=60)

            if response.status_code == 200:
                self.current_image_data = response.content
                img = Image.open(BytesIO(self.current_image_data))
                
                # Resize for preview
                preview_width = self.right_frame.winfo_width()
                preview_height = self.right_frame.winfo_height()
                img.thumbnail((preview_width, preview_height))
                
                photo = ImageTk.PhotoImage(img)
                self.image_label.config(image=photo, text="")
                self.image_label.image = photo 
                self.save_button.config(state="normal")
            else:
                self.image_label.config(text=f"API Error: {response.status_code}")
                messagebox.showerror("API Error", response.text)

        except Exception as e:
            self.image_label.config(text=f"Error: {e}")
            messagebox.showerror("Error", str(e))

    def save_image(self):
        if not self.current_image_data: return
        
        save_path = filedialog.asksaveasfilename(
            defaultextension=".jpg",
            filetypes=[("JPEG Image", "*.jpg"), ("All files", "*.*")],
            title="Save Image As..."
        )
        if save_path:
            with open(save_path, 'wb') as f:
                f.write(self.current_image_data)
            messagebox.showinfo("Success", f"Saved to {os.path.basename(save_path)}")


class EmilyManagerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Emily OS Manager v1.0")
        self.root.geometry("600x600")

        if not os.path.exists(PERSONA_DIR):
            os.makedirs(PERSONA_DIR)

        main_frame = tk.Frame(root)
        main_frame.pack(padx=15, pady=15, fill="both", expand=True)

        # --- Section 1: Persona / Save Slots ---
        persona_frame = tk.LabelFrame(main_frame, text="Persona & Save Slots", padx=10, pady=10)
        persona_frame.pack(pady=5, fill="x")

        self.persona_listbox = tk.Listbox(persona_frame, height=5)
        self.persona_listbox.pack(pady=5, fill="x")
        
        btn_frame = tk.Frame(persona_frame)
        btn_frame.pack(fill="x")
        
        tk.Button(btn_frame, text="Load Selected", command=self.load_persona).pack(side="left", expand=True, fill="x", padx=2)
        tk.Button(btn_frame, text="Save Current State", command=self.save_persona).pack(side="left", expand=True, fill="x", padx=2)
        tk.Button(btn_frame, text="Refresh", command=self.refresh_persona_list).pack(side="left", expand=True, fill="x", padx=2)

        # --- Section 2: File Actions ---
        actions_frame = tk.LabelFrame(main_frame, text="Device Management", padx=10, pady=10)
        actions_frame.pack(pady=5, fill="x")
        
        tk.Button(actions_frame, text="Upload System Prompt (.txt)", command=self.upload_system_prompt).pack(fill="x", pady=2)
        tk.Button(actions_frame, text="Upload Tools Config (.json)", command=self.upload_tools_config).pack(fill="x", pady=2)
        tk.Button(actions_frame, text="Upload Game Data (adventure.json)", command=self.upload_adventure_json, fg="blue").pack(fill="x", pady=2)
        
        sep = tk.Frame(actions_frame, height=2, bd=1, relief="sunken")
        sep.pack(fill="x", pady=5)
        
        tk.Button(actions_frame, text="Clear Chat History on Device", command=self.delete_chat_history, fg="red").pack(fill="x", pady=2)
        
        # --- Section 3: Tools ---
        tk.Button(main_frame, text="Open Art Gallery (Image Generator)", command=self.open_image_extractor, height=2, bg="#ddd").pack(fill="x", pady=10)
        
        # --- Log ---
        log_frame = tk.LabelFrame(main_frame, text="System Log", padx=10, pady=10)
        log_frame.pack(pady=5, fill="both", expand=True)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=6, wrap=tk.WORD)
        self.log_text.pack(fill="both", expand=True)

        self.refresh_persona_list()
    
    def open_image_extractor(self):
        ImageGalleryApp(self.root)

    def _log(self, message):
        timestamp = datetime.datetime.now().strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.log_text.see(tk.END)

    def _upload_file(self, remote_filename, content):
        try:
            url = f"http://{EMILY_BRAIN_IP}:{EMILY_BRAIN_PORT}/upload?file={remote_filename}"
            self._log(f"Uploading {remote_filename} to {EMILY_BRAIN_IP}...")
            response = requests.post(url, data=content.encode('utf-8'), headers={'Content-Type': 'text/plain'}, timeout=10)
            if response.status_code == 200:
                self._log("Upload Successful.")
                return True
            else:
                self._log(f"Upload Failed: {response.status_code}")
                return False
        except Exception as e:
            self._log(f"Connection Error: {e}")
            return False

    def _download_file(self, remote_filename):
        try:
            url = f"http://{EMILY_BRAIN_IP}:{EMILY_BRAIN_PORT}/download?file={remote_filename}"
            self._log(f"Downloading {remote_filename}...")
            response = requests.get(url, timeout=10)
            if response.status_code == 200:
                return response.text
            self._log(f"Download Failed: {response.status_code}")
            return None
        except Exception as e:
            self._log(f"Connection Error: {e}")
            return None

    def refresh_persona_list(self):
        self.persona_listbox.delete(0, tk.END)
        if os.path.exists(PERSONA_DIR):
            personas = [d for d in os.listdir(PERSONA_DIR) if os.path.isdir(os.path.join(PERSONA_DIR, d))]
            for p in sorted(personas):
                self.persona_listbox.insert(tk.END, p)
        self._log("Persona list refreshed.")

    def save_persona(self):
        persona_name = simpledialog.askstring("Save", "Step 1: Enter Persona Name (e.g., DungeonMaster):")
        if not persona_name: return

        save_slot_name = simpledialog.askstring("Save", "Step 2: Enter Save Slot Name (e.g., Session_1):")
        if not save_slot_name: return

        save_path = os.path.join(PERSONA_DIR, persona_name, "save_slots", save_slot_name)
        if os.path.exists(save_path):
            if not messagebox.askyesno("Overwrite", "This slot exists. Overwrite?"):
                return
        else:
            os.makedirs(save_path)

        # Download files from EmilyBrain
        self._log("Backing up current state from device...")
        prompt = self._download_file("/system_prompt.txt")
        tools = self._download_file("/tools_config.json")
        history = self._download_file("/chat_history.jsonl")
        
        # Also try to grab adventure.json if it exists
        adventure = self._download_file("/adventure.json")

        if not all([prompt, tools]):
            messagebox.showerror("Error", "Critical files could not be downloaded from device.")
            return

        # Save locally
        with open(os.path.join(save_path, "system_prompt.txt"), "w", encoding='utf-8') as f: f.write(prompt)
        with open(os.path.join(save_path, "tools_config.json"), "w", encoding='utf-8') as f: f.write(tools)
        if history:
            with open(os.path.join(save_path, "chat_history.jsonl"), "w", encoding='utf-8') as f: f.write(history)
        if adventure:
             with open(os.path.join(save_path, "adventure.json"), "w", encoding='utf-8') as f: f.write(adventure)

        self._log(f"Saved state to '{persona_name}/{save_slot_name}'.")
        self.refresh_persona_list()
        messagebox.showinfo("Success", "State saved successfully.")

    def load_persona(self):
        selected_indices = self.persona_listbox.curselection()
        if not selected_indices: return
        
        persona_name = self.persona_listbox.get(selected_indices[0])
        save_slots_path = os.path.join(PERSONA_DIR, persona_name, "save_slots")

        if not os.path.exists(save_slots_path) or not os.listdir(save_slots_path):
            messagebox.showwarning("Empty", "No save slots found for this persona.")
            return

        slot_window = tk.Toplevel(self.root)
        slot_window.title(f"Load {persona_name}")
        slot_window.geometry("300x250")
        
        tk.Label(slot_window, text="Select Session to Load:").pack(pady=5)
        slot_listbox = tk.Listbox(slot_window)
        slot_listbox.pack(padx=10, pady=5, fill="both", expand=True)
        
        for slot in sorted(os.listdir(save_slots_path)):
            slot_listbox.insert(tk.END, slot)

        def on_load_slot():
            sel = slot_listbox.curselection()
            if not sel: return
            slot_name = slot_listbox.get(sel[0])
            slot_window.destroy()
            self._execute_load(persona_name, slot_name)

        tk.Button(slot_window, text="Load & Overwrite Device", command=on_load_slot, bg="red", fg="white").pack(pady=10)

    def _execute_load(self, persona_name, slot_name):
        if not messagebox.askyesno("Confirm", f"Overwrite Emily's brain with '{slot_name}'?"):
            return

        slot_path = os.path.join(PERSONA_DIR, persona_name, "save_slots", slot_name)
        try:
            # Mandatory files
            with open(os.path.join(slot_path, "system_prompt.txt"), "r", encoding='utf-8') as f: prompt = f.read()
            with open(os.path.join(slot_path, "tools_config.json"), "r", encoding='utf-8') as f: tools = f.read()
            
            # Optional files
            history = ""
            if os.path.exists(os.path.join(slot_path, "chat_history.jsonl")):
                with open(os.path.join(slot_path, "chat_history.jsonl"), "r", encoding='utf-8') as f: history = f.read()
            
            adventure = ""
            if os.path.exists(os.path.join(slot_path, "adventure.json")):
                 with open(os.path.join(slot_path, "adventure.json"), "r", encoding='utf-8') as f: adventure = f.read()

        except Exception as e:
            messagebox.showerror("Error", f"Read failed: {e}")
            return

        self._log("Uploading new state...")
        s1 = self._upload_file("/system_prompt.txt", prompt)
        s2 = self._upload_file("/tools_config.json", tools)
        s3 = True
        if history: s3 = self._upload_file("/chat_history.jsonl", history)
        s4 = True
        if adventure: s4 = self._upload_file("/adventure.json", adventure)

        if all([s1, s2, s3, s4]):
            messagebox.showinfo("Success", "Loaded! Please restart EmilyBrain.")
        else:
            messagebox.showerror("Error", "Some files failed to upload.")

    def upload_system_prompt(self):
        path = filedialog.askopenfilename(title="Select system_prompt.txt")
        if path:
            with open(path, 'r', encoding='utf-8') as f: content = f.read()
            self._upload_file("/system_prompt.txt", content)

    def upload_tools_config(self):
        path = filedialog.askopenfilename(title="Select tools_config.json")
        if path:
            with open(path, 'r', encoding='utf-8') as f: content = f.read()
            self._upload_file("/tools_config.json", content)
    
    def upload_adventure_json(self):
        path = filedialog.askopenfilename(title="Select adventure.json")
        if path:
            with open(path, 'r', encoding='utf-8') as f: content = f.read()
            self._upload_file("/adventure.json", content)

    def delete_chat_history(self):
        if not messagebox.askyesno("Confirm", "Are you sure you want to wipe Emily's memory?"):
            return
        try:
            url = f"http://{EMILY_BRAIN_IP}:{EMILY_BRAIN_PORT}/delete-history"
            response = requests.get(url, timeout=10)
            if response.status_code == 200:
                self._log("Memory wiped successfully.")
                messagebox.showinfo("Success", "Memory wiped.")
            else:
                self._log(f"Error: {response.text}")
        except Exception as e:
            self._log(f"Error: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = EmilyManagerApp(root)
    root.mainloop()