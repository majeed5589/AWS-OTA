# ESP32 OTA Updates with AWS IoT and S3

## Configuring AWS S3 Bucket for OTA Updates

### Creating an S3 Bucket with Versioning

1. Log in to the AWS Management Console and navigate to the S3 service.
2. Click "Create bucket" to start the bucket creation process.
3. Enter a globally unique bucket name (e.g., `esp32-firmware-updates`).
4. Choose your preferred AWS region (this should be the same region as your IoT resources).
5. Under "Bucket Versioning," select "Enable" to maintain multiple versions of your firmware files. This is important for managing firmware rollbacks if needed.
6. Continue to the next section of the configuration.

### Setting Up Object Ownership and ACLs

1. In the "Object Ownership" section, select "ACLs enabled" option.
2. Choose "Object Writer".
3. Under "Block Public Access settings for this bucket," uncheck "Block all public access" to allow public read access to your firmware files.
4. Acknowledge the warning by checking the confirmation box.
5. Complete the bucket creation process by clicking "Create bucket."

### Configuring Bucket Permissions and Policies

1. After creating the bucket, select it from your bucket list.
2. Go to the "Permissions" tab.
3. Under "Bucket Policy," click "Edit" and paste the following policy (replace `YOUR-BUCKET-NAME` with your actual bucket name):

```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Sid": "PublicReadForFirmwareFiles",
            "Effect": "Allow",
            "Principal": "*",
            "Action": "s3:GetObject",
            "Resource": "arn:aws:s3:::YOUR-BUCKET-NAME/*"
        }
    ]
}
```

4. Click "Save changes" to apply the bucket policy.

### Setting Up CORS Configuration

1. While still in the "Permissions" tab, scroll down to "Cross-origin resource sharing (CORS)."
2. Click "Edit" and paste the following CORS configuration:

```json
[
    {
        "AllowedHeaders": [
            "*"
        ],
        "AllowedMethods": [
            "GET"
        ],
        "AllowedOrigins": [
            "*"
        ],
        "ExposeHeaders": []
    }
]
```

3. Click "Save changes" to apply the CORS configuration.

## Uploading and Configuring Firmware Files

### Uploading Your Firmware Binary

1. Navigate to your S3 bucket in the AWS Console.
2. Click "Upload" to begin the upload process.
3. Select your firmware binary file (`.bin` extension) and upload it.

### Setting Object ACL for Public Access

1. After the upload completes, select your firmware file from the bucket contents.
2. Go to the "Permissions" tab for this specific object.
3. Under "Object Access Control List (ACL)," click "Edit."
4. Check the "Read" permission box for "Everyone (public access) for both Object and Object ACL"
5. Save your changes to make the object publicly readable.

### Getting the Firmware Object URL

1. While still viewing your firmware file, go to the "Properties" tab.
2. Scroll down to find the "Object URL." It should look like:
   ```
   https://YOUR-BUCKET-NAME.s3.YOUR-REGION.amazonaws.com/firmware.bin
   ```
3. Copy this URL - you'll need it to trigger OTA updates.

## Triggering OTA Updates

### Method 1: Sending a Global Update Notification

To trigger an OTA update for all ESP32 devices in your fleet:

1. Open the AWS IoT Core console and navigate to "Test" → "MQTT test client."
2. In the "Publish" section, enter the topic: `all_devices/ota_notification`
3. Enter the following JSON payload (replace the URL and version with your actual values):

```json
{
  "action": "update_available",
  "firmwareVersion": "1.0.1",
  "firmwareUrl": "https://YOUR-BUCKET-NAME.s3.YOUR-REGION.amazonaws.com/firmware.bin"
}
```

4. Click "Publish" to send the update notification to all devices.
5. Each ESP32 device subscribed to this topic will display an update notification on its Serial Monitor:

```
========================================
   FIRMWARE UPDATE AVAILABLE
========================================
Current version: 1.0.0
New version: 1.0.1
Type 'download' to start the update process
========================================
```

6. To initiate the update on each device, open the Serial Monitor and type `download` followed by Enter/Return.
7. The device will confirm the update request, download the new firmware, and automatically reboot with the updated version.

### Monitoring Update Status

To monitor the update process across your device fleet:

1. In the MQTT test client, subscribe to these topics:
   - `device_updates/confirmation` - Shows which devices have accepted the update
   - `+/update_status` - Shows update progress for all devices (+ is a wildcard)

2. You'll see messages showing the update progress for each device, from initiation through completion.



1. Use pre-signed URLs instead of public bucket access
2. Implement firmware signing and verification
3. Use specific AWS IoT policies rather than the broad permissions shown
4. Consider implementing fleet management with AWS IoT Jobs for more controlled rollouts

This more secure approach provides the convenience of OTA updates while maintaining appropriate security for your IoT devices.
