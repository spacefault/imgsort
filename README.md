# imgsort

sort images and videos taken with digital cameras chronologically using `DateTimeOriginal`, requires `exiftool`.

useful when compiling a collection of photos taken from multiple cameras.

# usage

`imgsort [--dry-run] <directory> <base name>`

`--dry-run` to see what changes would be made, usually you'll get an output like:

```
[dry-run] IMG_3945.PNG -> Japan_2006_001.PNG
[dry-run] IMG_2912.PNG -> Japan_2006_002.PNG
[dry-run] IMG_4952.MOV -> Japan_2006_003.MOV
[dry-run] IMG_5914.PNG -> Japan_2006_004.PNG
```

order is made using the `DateTimeOriginal` exif value, but other ones like `CreateDate`, `MediaCreateDate`, and `TrackCreateDate` may be used. as a last resort, `FileCreateDate` is used for images with absolutely no metadata or if the data cannot be found. ranking is done by translating the timestamp to unix millis, and comparing it with the rest of the converted image data for accuracy.

# compile
`make` to make.

