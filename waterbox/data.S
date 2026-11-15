/* data.S - the data the core carries (data-pack.py builds the blob): linked
 * into the core's read-only image, so it is in no savestate */
	.section .rodata.s3air_data,"a"
	.balign 64
	.global s3air_data
s3air_data:
	.incbin S3AIR_DATA_BLOB
	.global s3air_data_end
s3air_data_end:
	.section .note.GNU-stack,"",@progbits
