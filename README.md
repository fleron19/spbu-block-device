# Linux block device
Linux bio-based 4MiB RAM block device, made for educationsl purposes

## Environment
- Fedora server 43
- Linux kernel 6.18.13
- Requirements: gcc, make, kernel-devel to build

## Usage
### Clone and build
```bash
git clone https://github.com/fleron19/spbu-block-device
cd spbu-block-device/src && make
```
### Insert and mount
```bash
sudo insmod block-device.ko
```
Once module is loaded, `block-device` will appear in `lsmod` and in `/dev`
Now you can use it as any other device. For example, format and mount:
```bash
sudo mkfs.ext4 /dev/block-device 
sudo mount -t ext4 /dev/block-device /mnt/block-device
```
### Unmount and unload
To remove block-device from your system you should use `rmmod`. Be careful, after `rnmod` all data, that was stored on device, would be permanently lost. Also, if you mounted device, you should `umout` it. For example:
```bash
sudo umount /mnt/block-device
sudo rmmod block-device
```
## Testing
TODO


