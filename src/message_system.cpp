#include "message_system.h"
#include <lvgl.h>
#include <cstdio>
#include "logger.h"
DEFINE_MODULE_LOGGER(MessageLog)

namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x050812;
constexpr uint32_t COLOR_MINT = 0x58F5C9;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;

constexpr uint32_t MESSAGE_SCROLL_PX_PER_SEC = 60;
constexpr uint32_t MESSAGE_MIN_DURATION_MS = 3500;
constexpr uint32_t MESSAGE_MAX_DURATION_MS = 14000;
constexpr int16_t MESSAGE_LIST_HEIGHT = 70;
constexpr bool MESSAGE_LIST_VISIBLE = false;
constexpr const char* kFallbackMessage = "THÔNG ĐIỆP MỚI";

static const char* kMessageItems[] = {
  "Tiền đang tìm đường đến nhà bạn, đừng khóa cửa nhé!",
  "Ví của bạn sắp \"tăng cân\" đột ngột đấy.",
  "Thần tài đang gõ cửa, nhưng hình như bạn đang đeo tai nghe?",
  "Hôm nay số đào hoa, ngày mai số đào được vàng.",
  "Vận may của bạn dày hơn lớp trà sữa trân châu bạn uống hôm nay.",
  "Tài lộc đang đến, hãy chuẩn bị một cái túi thật to!",
  "Số của bạn là số hưởng, chỉ cần đợi đúng thời điểm thôi.",
  "Đừng từ bỏ giấc mơ của mình. Hãy đi ngủ tiếp.",
  "Người ấy đang nghĩ về bạn... hoặc đang nghĩ về việc tối nay ăn gì.",
  "Trái tim bạn sắp có \"biến\", một biến số mang tên hạnh phúc.",
  "Đừng tìm tình yêu nữa, nó đang đứng ngay sau lưng bạn (đừng quay lại vội, kẻo giật mình).",
  "Sắp có một tin nhắn làm bạn mỉm cười cả ngày.",
  "Crush cũng thích bạn đấy, nhưng là \"thích\" ảnh của bạn thôi.",
  "Tình duyên nở rộ như hoa mười giờ, nhưng hy vọng nó kéo dài hơn thế.",
  "Nếu hôm nay không vui, hãy nhớ rằng ngày mai... cũng chưa chắc vui hơn (đùa thôi!).",
  "Hãy luôn là chính mình, trừ khi bạn có thể trở thành Batman.",
  "Làm việc chăm chỉ sẽ không làm bạn chết, nhưng tại sao phải mạo hiểm?",
  "Đừng lo lắng về tương lai, nó chưa đến đâu mà lo.",
  "Ăn thêm một cái bánh nữa đi, vận may nằm ở cái tiếp theo ấy.",
  "Cuộc đời là những chuyến đi, đi ngủ là một trong số đó.",
  "Bạn đẹp nhất khi bạn... là chính mình (và khi vừa nhận lương).",
  "Hôm nay là một ngày đẹp trời để làm điều gì đó điên rồ.",
  "Thế giới này cần nụ cười của bạn, nên hãy cười lên nhé!",
  "Mọi chuyện rồi sẽ ổn, nếu không ổn thì ăn một miếng bánh là ổn.",
  "Bạn là phiên bản giới hạn, đừng để ai biến bạn thành bản photocopy.",
  "Mặt trời luôn mọc, dù hôm qua bạn có thức khuya xem phim đi chăng nữa.",
  "Cứ đi rồi sẽ đến, cứ ăn rồi sẽ no.",
  "Im lặng là vàng, nhưng nói lời hay là kim cương.",
  "Đừng nhìn lại, quá khứ không có gì mới đâu.",
  "Mọi con đường đều dẫn đến... tủ lạnh.",
  "Hạnh phúc là một lựa chọn, nhưng bạn đã chọn đúng câu này!",
  "Không phải mọi ngày đều cần phải tiến lên.",
  "Nếu hôm nay chậm hơn một chút, thế giới vẫn ổn.",
  "Có những việc không cần làm tốt, chỉ cần làm tới.",
  "Bạn đã cố gắng nhiều hơn bạn nghĩ.",
  "Im lặng đôi khi là một câu trả lời.",
  "Không sao nếu bạn chưa biết tiếp theo là gì.",
  "Một ngày bình thường cũng là một ngày đáng giữ lại.",
  "Đừng quên thở. Nhẹ thôi cũng được.",
  "Có những thứ không cần được giải quyết ngay.",
  "Bạn không đến trễ. Bạn đến đúng nhịp của mình.",
  "Hôm nay có vẻ ổn. Nhưng đừng chủ quan, vũ trụ ghét người tự tin.",
  "Bạn sẽ gặp may mắn… nếu ra khỏi giường trước 10h.",
  "Tránh xa người tên Huy hôm nay. Chỉ là cảm giác thôi.",
  "Cà phê hôm nay ngon hơn hôm qua. Nhưng bạn vẫn trễ deadline.",
  "Đừng bắt đầu mối quan hệ mới. Bánh tráng trộn không chữa được trái tim vỡ.",
  "Uống nước đi",
  "Ai đó đang nghĩ về bạn. Chắc là đang chửi.",
  "Tiền sẽ đến. Nhưng rồi sẽ đi. Rất nhanh.",
  "Đừng chơi đá gà cảm xúc. Bạn sẽ thua.",
  "Có thể bạn đúng. Nhưng to tiếng sẽ làm bạn sai.",
  "Thử im lặng hôm nay. Sự bí ẩn là vũ khí.",
  "Ai đó sẽ làm bạn cười. Có thể là chính bạn, khi soi gương.",
  "Cẩn thận lời nói. Mồm đi trước não là đặc sản rồi.",
  "Bụng đói là bụng nóng. Nạp năng lượng trước khi ai đó bị ăn tươi.",
  "Tình yêu đến khi bạn không kỳ vọng. Hoặc khi bạn thơm.",
  "Đi đường vòng có thể lâu hơn, nhưng đôi khi ít kẹt xe hơn.",
  "Người bạn ghét đang hạnh phúc. Học cách buông bỏ, hoặc unfollow.",
  "Trả lời tin nhắn đi. Người ta đang chờ. Có thể là nhà mạng.",
  "Bạn cần ngủ. Mắt bạn trông như bánh tráng nhúng nước.",
  "Lì xì tâm linh hôm nay: nhận ít nhưng đòi nhiều.",
  "Dừng lại, thở sâu, rồi tiếp tục giả vờ bạn ổn.",
  "Lạc quan lên. Hôm nay bạn chỉ bị xui nhẹ.",
  "Ăn phở hôm nay, không ai có thể ngăn bạn được.",
  "Đừng tin vào vận may. Tin vào bản thân… hoặc vào Google Maps.",
  "Đừng xem lại tin nhắn cũ. Tự hại mình để làm gì?",
  "Cười nhiều hơn hôm nay. Người ta sẽ nghĩ bạn biết bí mật gì đó.",
  "Bỏ qua lỗi lầm cũ. Không phải của người khác. Của bạn.",
  "Có cơ hội mới đang tới. Nhớ mở cửa.",
  "Bạn chưa hết thời. Mới chỉ… hơi mốc thôi.",
  "Tình yêu là giả, hóa đơn là thật.",
  "Bật chế độ bay. Trốn đời một tí cũng được.",
  "Không phải ai nói thương bạn cũng mua trà sữa cho bạn.",
  "Một người lạ sẽ giúp bạn. Có thể là shipper.",
  "Hôm nay là ngày hoàn hảo để tha thứ… hoặc tắt điện thoại.",
  "Ngủ muộn khiến bạn mộng mị. Mộng mị khiến bạn… trễ học.",
  "Bạn đang ổn. Bubu xác nhận.",
  "Trà sữa không giải quyết được mọi thứ. Nhưng là khởi đầu tốt.",
  "Dù ai nói ngả nói nghiêng, bạn vẫn phải đi làm.",
  "Cẩn thận với đồ ăn cay. Bụng bạn đang yếu lòng.",
  "Đừng thử may mắn hôm nay. Nó đang bận với người khác.",
  "Một cú lướt TikTok có thể thay đổi tâm trạng bạn. Hoặc hủy hoại nó.",
  "Hôm nay là ngày tốt để xóa mấy app độc hại.",
  "Thử mặc đồ khác màu. Biết đâu đổi luôn vận.",
  "Ngày đẹp để im lặng trong group chat.",
  "Có người nói dối bạn. Có thể là chính bạn.",
  "Không ai nhớ lỗi lầm của bạn trừ trí nhớ của bạn. Tha cho mình đi.",
  "Mọi chuyện rồi sẽ ổn. Nếu không ổn thì chưa hết chuyện.",
  "Ai đó đang stalk bạn. Mà bạn lại đang post nhảm.",
  "Cười ít lại, đừng lộ bài.",
  "Hôm nay, bạn chính là… nhân vật phụ đáng yêu.",
  "Nhắm mắt lại. Nghĩ về điều tốt đẹp. Không có? Tạo ra đi.",
  "Nắng lên rồi. Nhưng đừng để ảo tưởng lên theo.",
  "Có người thương bạn, nhưng còn ngại. Có thể là Bubu.",
  "Hôm nay tốt cho tóc. Nhưng không cho tình duyên.",
  "Đừng xin vía nữa. Bạn cần xin deadline.",
  "Đang ổn định? Đó là lúc bão tới.",
  "Có ai đó đang nhớ bạn… để đòi nợ.",
  "Tránh xa drama. Bạn không có điều kiện tinh thần.",
  "Bạn là ngọn lửa. Nhưng đừng đốt luôn deadline.",
  "Tập thể dục 5 phút. Đủ để Bubu bớt lo.",
  "Tin vào nhân quả. Bún đậu nay, dạ dày mai.",
  "Hôm nay là ngày đẹp để… cắn môi và giả vờ đang suy nghĩ.",
  "Bạn không thất bại. Bạn chỉ đang thử bản beta.",
  "Ai đó nói xấu bạn. Nhưng bằng ngữ pháp sai.",
  "Đừng edit ảnh quá đà. Người ta ngoài đời sẽ bất ngờ.",
  "Đừng đánh giá ngày qua bài post. Đó là highlight, không phải sự thật.",
  "Tình yêu như ổ điện. Đừng thò tay khi không biết dây nào nóng.",
  "Dự đoán: hôm nay bạn sẽ quên điều quan trọng. Kiểm tra ví đi.",
  "Bạn chưa bị ghét, chỉ là người ta mệt bạn.",
  "Đừng đi ăn một mình tối nay. Cảm xúc sẽ ăn bạn lại.",
  "Hôm nay đẹp trời. Nhưng đừng quên mang áo mưa.",
  "Nhạc buồn nên dừng lại. Trừ khi bạn muốn hóa mây.",
  "Có thể bạn không sai. Nhưng bạn hơi lạ.",
  "Mơ lớn. Nhưng nợ nhỏ thôi.",
  "Hãy sống như lá me bay – nhẹ nhàng, khó đoán, và không mắc nợ.",
  "Hôm nay là ngày tốt để bắt đầu lại. Nhưng không phải với người yêu cũ.",
  "Bạn đang ở đúng chỗ. Chỉ sai thời điểm thôi.",
  "Hôm nay, mọi thứ trông có vẻ chán. Có thể là do bạn.",
  "Bubu thấy bạn ổn. Nhưng còn hơi… cần ngủ.",
  "Thử nói thật lòng hôm nay. Nhẹ lòng, hoặc mất bạn.",
  "Đừng chia sẻ quá nhiều. Có thể bạn đang nói với group có người \"bỏ vô sọt rác.\"",
  "Hôm nay bạn có cơ hội. Nhớ mở mắt.",
  "Bạn không một mình. Wi-Fi cũng đang khóc.",
  "Thử đứng trước gương và cười. Nếu gương nứt, chạy đi.",
  "Đừng gửi tin nhắn lúc 2 giờ sáng. Đừng.",
  "Bạn cần một chuyến đi. Không cần xa, chỉ cần không ngồi im.",
  "Ai đó sẽ khiến bạn cười. Có thể là Bubu run khi thấy ổ cắm.",
  "Tâm bạn động. Nhưng ví bạn đừng động theo.",
  "Tình duyên sẽ ổn. . . sau vài lần sụp đổ nữa.",
  "Đừng tìm ai đó chữa lành. Hãy tự vá mình trước.",
  "Bắt đầu lại từ… việc dọn phòng.",
  "Hôm nay, bớt nói. Nhiều người đang mệt bạn rồi.",
  "Có thể bạn chưa biết: hôm nay trời đẹp. Mắt bạn đang mờ.",
  "Đừng uống ly cà phê thứ 4. Nhịp tim bạn đang mệt.",
  "Ai đó thích bạn. Nhưng còn sống ảo nên chưa dám nói.",
  "Gửi một lời khen đi. Không ai cản bạn đâu.",
  "Đừng tin vào chỉ tay. Tin vào đôi tay bạn.",
  "Hôm nay, bạn sẽ khiến ai đó nhớ mãi. Cẩn thận làm gì.",
  "Tắt màn hình đi. Mắt bạn đang chửi bạn.",
  "Bubu thấy bạn. Và Bubu nghĩ… bạn đang làm tốt hơn bạn nghĩ."
};
constexpr size_t kMessageCount = sizeof(kMessageItems) / sizeof(kMessageItems[0]);
constexpr size_t kTotalMessageCount = kMessageCount + 1;
int lastMessageIndex = -1;
char luckyMessage[64];

lv_obj_t* messagePanel = nullptr;
lv_obj_t* messageList = nullptr;
lv_obj_t* messageLabel = nullptr;
bool messageOpen = false;
MessageSystem::FinishedCallback finishedCb = nullptr;

static const char* buildLuckyMessage() {
  int lucky = static_cast<int>(random(1, 101));
  snprintf(luckyMessage, sizeof(luckyMessage), "Con số may mắn hôm nay: %d.", lucky);
  return luckyMessage;
}

static const char* nextMessage() {
  if (kTotalMessageCount == 1) {
    return buildLuckyMessage();
  }

  size_t idx = 0;
  do {
    idx = static_cast<size_t>(random(static_cast<long>(kTotalMessageCount)));
  } while (static_cast<int>(idx) == lastMessageIndex);
  lastMessageIndex = static_cast<int>(idx);

  if (idx == kMessageCount) {
    return buildLuckyMessage();
  }
  return kMessageItems[idx];
}

static void populateMessageList() {
  if (!messageList) return;
  lv_obj_clean(messageList);
  if (!MESSAGE_LIST_VISIBLE) return;
  lv_obj_set_flex_flow(messageList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(messageList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(messageList, 6, 0);
  for (size_t i = 0; i < kMessageCount; ++i) {
    lv_obj_t* item = lv_label_create(messageList);
    lv_label_set_text(item, kMessageItems[i]);
    lv_label_set_long_mode(item, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(item, lv_pct(100));
    lv_obj_set_style_text_font(item, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
  }
}

static void createMessagePanel() {
  if (messagePanel != nullptr) return;

  messagePanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(messagePanel, 240, 240);
  lv_obj_center(messagePanel);
  lv_obj_set_style_radius(messagePanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(messagePanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(messagePanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(messagePanel, 12, 0);
  lv_obj_set_style_border_color(messagePanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(messagePanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(messagePanel, 0, 0);
  lv_obj_clear_flag(messagePanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(messagePanel, LV_OBJ_FLAG_HIDDEN);

  messageList = lv_obj_create(messagePanel);
  lv_obj_set_size(messageList, 200, MESSAGE_LIST_HEIGHT);
  lv_obj_align(messageList, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_set_style_bg_opa(messageList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(messageList, 0, 0);
  lv_obj_set_style_pad_all(messageList, 0, 0);
  lv_obj_set_scroll_dir(messageList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(messageList, LV_SCROLLBAR_MODE_OFF);

  messageLabel = lv_label_create(messagePanel);
  lv_obj_set_style_text_color(messageLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(messageLabel, &lv_font_montserrat_vn_22, 0);
  lv_label_set_long_mode(messageLabel, LV_LABEL_LONG_MODE_CLIP);
  lv_label_set_text(messageLabel, "");
  lv_obj_set_size(messageLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

  populateMessageList();
}

static void messageAnimXCb(void* var, int32_t v) {
  lv_obj_set_x(static_cast<lv_obj_t*>(var), v);
}

static void messageAnimReadyCb(lv_anim_t* a) {
  (void)a;
  if (finishedCb) {
    MessageSystem::FinishedCallback cb = finishedCb;
    finishedCb = nullptr;
    cb();
  } else {
    MessageSystem::close();
  }
}

static void stopMessageMarquee() {
  if (!messageLabel) return;
  lv_anim_del(messageLabel, messageAnimXCb);
}

static void startMessageMarquee(const char* text) {
  if (!messagePanel || !messageLabel) return;

  stopMessageMarquee();
  lv_label_set_text(messageLabel, text);
  lv_obj_update_layout(messageLabel);

  int16_t panelW = lv_obj_get_width(messagePanel);
  int16_t panelH = lv_obj_get_height(messagePanel);
  int16_t labelW = lv_obj_get_width(messageLabel);
  int16_t labelH = lv_obj_get_height(messageLabel);
  int16_t y = static_cast<int16_t>((panelH - labelH) / 2);

  int32_t startX = panelW;
  int32_t endX = -labelW;
  uint32_t distance = static_cast<uint32_t>(panelW + labelW);
  uint32_t duration = (distance * 1000U) / MESSAGE_SCROLL_PX_PER_SEC;
  if (duration < MESSAGE_MIN_DURATION_MS) duration = MESSAGE_MIN_DURATION_MS;
  if (duration > MESSAGE_MAX_DURATION_MS) duration = MESSAGE_MAX_DURATION_MS;

  lv_obj_set_y(messageLabel, y);
  lv_obj_set_x(messageLabel, startX);

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, messageLabel);
  lv_anim_set_exec_cb(&anim, messageAnimXCb);
  lv_anim_set_values(&anim, startX, endX);
  lv_anim_set_time(&anim, duration);
  lv_anim_set_path_cb(&anim, lv_anim_path_linear);
  lv_anim_set_ready_cb(&anim, messageAnimReadyCb);
  lv_anim_start(&anim);
}

}  // namespace

namespace MessageSystem {

void begin() {
  createMessagePanel();
}

void open(FinishedCallback onFinished) {
  createMessagePanel();
  finishedCb = onFinished;
  messageOpen = true;
  lv_obj_clear_flag(messagePanel, LV_OBJ_FLAG_HIDDEN);
  stopMessageMarquee();
  startMessageMarquee(nextMessage());
}

void close() {
  if (!messagePanel) return;
  stopMessageMarquee();
  lv_obj_add_flag(messagePanel, LV_OBJ_FLAG_HIDDEN);
  messageOpen = false;
  finishedCb = nullptr;
}

bool isOpen() {
  return messageOpen;
}

}  // namespace MessageSystem
