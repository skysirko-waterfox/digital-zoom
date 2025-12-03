#!/usr/bin/env bash
set -euo pipefail

echo "==============================================================="
echo " Configuring network using systemd-networkd"
echo "==============================================================="

CAM_IF="eth0"
CAM_IP="192.168.200.11/24"

USB_PI_IP="192.168.201.1/24"

echo "==> Detecting USB Ethernet interface..."
USB_IF="$(ip -o link show | awk -F': ' '/^.*: enx/{print $2; exit}')"

if [[ -z "${USB_IF}" ]]; then
  echo "ERROR: USB-Ethernet adapter not detected."
  echo "Plug it in and re-run:"
  echo "  sudo ./configure_network_systemd.sh"
  exit 1
fi

echo "==> USB interface detected: ${USB_IF}"

## -------------------------------------------------------------------
## Disable NetworkManager (it interferes with systemd-networkd)
## -------------------------------------------------------------------
#echo "==> Disabling NetworkManager..."
#sudo systemctl disable --now NetworkManager.service || true
#sudo systemctl disable --now NetworkManager-wait-online.service || true

# -------------------------------------------------------------------
# Enable systemd-networkd + systemd-resolved
# -------------------------------------------------------------------
echo "==> Enabling systemd-networkd..."
sudo systemctl enable --now systemd-networkd.service
sudo systemctl enable --now systemd-resolved.service

# -------------------------------------------------------------------
# Create systemd-networkd config for eth0 (camera network)
# -------------------------------------------------------------------
echo "==> Creating config for camera interface (${CAM_IF})..."

sudo tee /etc/systemd/network/10-${CAM_IF}.network >/dev/null <<EOF
[Match]
Name=${CAM_IF}

[Network]
Address=${CAM_IP}
EOF

# -------------------------------------------------------------------
# Create systemd-networkd config for USB Ethernet (laptop link)
# -------------------------------------------------------------------
echo "==> Creating config for USB interface (${USB_IF})..."

sudo tee /etc/systemd/network/20-${USB_IF}.network >/dev/null <<EOF
[Match]
Name=${USB_IF}

[Network]
Address=${USB_PI_IP}
EOF

# -------------------------------------------------------------------
# Apply changes
# -------------------------------------------------------------------
echo "==> Reloading networkd..."
sudo systemctl restart systemd-networkd

echo ""
echo "==============================================================="
echo " DONE!"
echo ""
echo "Camera network:"
echo "  Orange Pi: ${CAM_IP} on ${CAM_IF}"
echo ""
echo "Laptop network:"
echo "  Orange Pi: ${USB_PI_IP} on ${USB_IF}"
echo "  Set laptop manually to: 192.168.201.2/24"
echo ""
echo "Reboot recommended: sudo reboot"
echo "==============================================================="
