
// this object stores nfc tag data
struct nfcTagObject {
  uint32_t cookie;
  uint8_t version;
  uint8_t folder;
  uint8_t mode;
  uint8_t special;
  uint32_t color;
};

void writeCard(nfcTagObject nfcTag);
void resetCard(void);
bool readCard(nfcTagObject *nfcTag);
void setupCard(void);

static void nextTrack();
void startTimer(void);
void stoppTimer(void);
int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0);

void dump_byte_array(byte *buffer, byte bufferSize);
