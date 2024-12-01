## dexopt_chroot_setup

dexopt_chroot_setup is a component of ART Service. It sets up the chroot
environment for Pre-reboot Dexopt, to dexopt apps on a best-effort basis before
the reboot after a Mainline update or an OTA update is downloaded, to support
seamless updates.

It requires elevated permissions that are not available to system_server, such
as mounting filesystems. It publishes a binder interface that is internal to ART
Service's Java code.

### Pre-reboot Dexopt file structure

#### Overview

```
/mnt/pre_reboot_dexopt
|-- chroot
|   |-- system_ext
|   |-- vendor
|   |-- product
|   |-- data
|   |-- mnt
|   |   |-- expand
|   |   `-- artd_tmp
|   |-- dev
|   |-- proc
|   |-- sys
|   |-- metadata
|   |-- apex
|   `-- linkerconfig
`-- mount_tmp
```

#### `/mnt/pre_reboot_dexopt`

The root directory for Pre-reboot Dexopt, prepared by `init`.

#### `/mnt/pre_reboot_dexopt/chroot`

The root directory of the chroot environment for Pre-reboot Dexopt. It is the
mount point of the `system` image. Created by `dexopt_chroot_setup`, and only
exists for the duration of the Pre-reboot Dexopt.

#### `/mnt/pre_reboot_dexopt/chroot/{system_ext,vendor,product}`

Mount points of other readonly images.

#### `/mnt/pre_reboot_dexopt/chroot/{data,mnt/expand,dev,proc,sys,metadata}`

Same as the corresponding directories outside of chroot. These are read-write
mounts.

#### `/mnt/pre_reboot_dexopt/chroot/mnt/artd_tmp`

An empty directory for storing temporary files during Pre-reboot Dexopt, managed
by `artd`.

#### `/mnt/pre_reboot_dexopt/chroot/apex`

For holding the apex mount points used in the chroot environment, managed by
`apexd`. Note that this is not the same as `/apex` outside of chroot.

#### `/mnt/pre_reboot_dexopt/chroot/linkerconfig`

For holding the linker config used in the chroot environment, managed by
`linkerconfig`. Note that this is not the same as `/linkerconfig` outside of
chroot.

#### `/mnt/pre_reboot_dexopt/mount_tmp`

An ephemeral directory used as a temporary mount point for bind-mounting
directories "slave+shared".
