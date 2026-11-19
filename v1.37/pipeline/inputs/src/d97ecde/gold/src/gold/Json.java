package gold;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** A small JSON reader (objects, arrays, strings, numbers, booleans, null), enough for program.json and input.json. */
public final class Json {
  private final String s;
  private int i;

  private Json(String s) { this.s = s; }

  public static Object parse(String text) {
    Json j = new Json(text);
    Object v = j.value();
    j.ws();
    if (j.i != text.length()) throw j.error("trailing characters");
    return v;
  }

  private RuntimeException error(String m) { return new IllegalArgumentException("JSON: " + m + " at " + i); }

  private void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++; }

  private Object value() {
    ws();
    if (i >= s.length()) throw error("unexpected end");
    char c = s.charAt(i);
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') return string();
    if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
    if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
    if (s.startsWith("null", i)) { i += 4; return null; }
    return number();
  }

  private Map<String, Object> object() {
    Map<String, Object> m = new LinkedHashMap<>();
    i++;
    ws();
    if (s.charAt(i) == '}') { i++; return m; }
    while (true) {
      ws();
      String k = string();
      ws();
      if (s.charAt(i++) != ':') throw error("expected ':'");
      m.put(k, value());
      ws();
      char c = s.charAt(i++);
      if (c == '}') return m;
      if (c != ',') throw error("expected ',' or '}'");
    }
  }

  private List<Object> array() {
    List<Object> a = new ArrayList<>();
    i++;
    ws();
    if (s.charAt(i) == ']') { i++; return a; }
    while (true) {
      a.add(value());
      ws();
      char c = s.charAt(i++);
      if (c == ']') return a;
      if (c != ',') throw error("expected ',' or ']'");
    }
  }

  private String string() {
    if (s.charAt(i) != '"') throw error("expected a string");
    StringBuilder b = new StringBuilder();
    i++;
    while (s.charAt(i) != '"') {
      char c = s.charAt(i++);
      if (c == '\\') {
        char e = s.charAt(i++);
        switch (e) {
          case 'n': b.append('\n'); break;
          case 't': b.append('\t'); break;
          case 'r': b.append('\r'); break;
          case 'b': b.append('\b'); break;
          case 'f': b.append('\f'); break;
          case 'u': b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; break;
          default: b.append(e);
        }
      } else {
        b.append(c);
      }
    }
    i++;
    return b.toString();
  }

  private Object number() {
    int start = i;
    while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
    String t = s.substring(start, i);
    if (t.isEmpty()) throw error("unexpected character");
    if (t.contains(".") || t.contains("e") || t.contains("E")) return Double.parseDouble(t);
    return Long.parseLong(t);
  }

  /** An unsigned integer member of an object, or null if absent. */
  public static Long u64(Map<?, ?> obj, String key) {
    Object v = obj.get(key);
    if (v == null) return null;
    if (!(v instanceof Long l) || l < 0) throw new IllegalArgumentException(key + ": not an unsigned integer");
    return l;
  }
}
