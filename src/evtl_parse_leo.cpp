/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   evtl_parse_leo.cpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nicolewicki <nicolewicki@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/27 10:10:44 by nicolewicki       #+#    #+#             */
/*   Updated: 2025/10/27 10:11:11 by nicolewicki      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

// static std::string lcase(std::string s){ for(char&c: s) c = std::tolower((unsigned char)c); return s; }
// static std::string trim(const std::string& s){
//     const char* ws = " \t\r\n";
//     auto a = s.find_first_not_of(ws); if (a==std::string::npos) return "";
//     auto b = s.find_last_not_of(ws);  return s.substr(a, b-a+1);
// }

// static bool parse_head_min(const std::string& head, HeadInfo& out){
//     // Startzeile
//     auto eol = head.find("\r\n"); if (eol==std::string::npos) return false;
//     std::istringstream iss(head.substr(0, eol));
//     if (!(iss >> out.method >> out.target >> out.version)) return false;
//     // Header
//     std::unordered_map<std::string,std::string> H;
//     size_t pos = eol + 2;
//     while (pos < head.size()) {
//         size_t next = head.find("\r\n", pos); if (next==std::string::npos) break;
//         std::string line = head.substr(pos, next-pos); pos = next + 2;
//         if (line.empty()) break;
//         auto c = line.find(':'); if (c==std::string::npos) return false;
//         std::string name = lcase(trim(line.substr(0,c)));
//         std::string val  = trim(line.substr(c+1));
//         H[name] = val;
//     }
//     // keep-alive
//     if (out.version == "HTTP/1.1") {
//         out.keep_alive = !(H.count("connection") && lcase(H["connection"])=="close");
//     } else if (out.version == "HTTP/1.0") {
//         out.keep_alive = (H.count("connection") && lcase(H["connection"])=="keep-alive");
//     } else {
//         out.keep_alive = false; // später ggf. 505
//     }
//     // body semantics
//     out.is_chunked = (H.count("transfer-encoding") && lcase(H["transfer-encoding"]).find("chunked")!=std::string::npos);
//     if (H.count("content-length")) out.content_length = std::strtoull(H["content-length"].c_str(), nullptr, 10);
//     return true;
// }

// verarbeitet so viel wie möglich in-place: aus c.rx konsumieren, de-chunked nach 'out' schieben.
// gibt true zurück, wenn Fortschritt; false, wenn mehr Daten benötigt werden.
// static bool dechunk_step(Client& c, std::string& out) {
//     using CS = Client::ChunkState;
//     for(;;){
//         if (c.ch_state == CS::SIZE) {
//             auto p = c.rx.find("\r\n");
//             if (p == std::string::npos) return false;
//             std::string line = c.rx.substr(0, p);
//             c.rx.erase(0, p+2);
//             // hex size; optional chunk extensions ignorieren
//             auto semi = line.find(';');
//             std::string hex = (semi==std::string::npos)? line : line.substr(0,semi);
//             c.ch_need = std::strtoull(hex.c_str(), nullptr, 16);
//             c.ch_state = (c.ch_need==0) ? CS::CRLF_AFTER_DATA : CS::DATA;
//         }
//         if (c.ch_state == CS::DATA) {
//             if (c.rx.size() < c.ch_need) return false;
//             out.append(c.rx.data(), c.ch_need);
//             c.rx.erase(0, c.ch_need);
//             c.ch_state = CS::CRLF_AFTER_DATA;
//         }
//         if (c.ch_state == CS::CRLF_AFTER_DATA) {
//             if (c.rx.size() < 2) return false;
//             if (c.rx.compare(0,2,"\r\n") != 0) return false; // malformed
//             c.rx.erase(0,2);
//             if (c.ch_need == 0) { c.ch_state = CS::DONE; return true; }
//             c.ch_state = CS::SIZE;
//         }
//     }
// }