Pod::Spec.new do |s|
  s.name = 'hoedown'
  s.version = '3.0.1'
  s.summary = 'Standards compliant, fast, secure markdown processing library in C. A fork of sundown.'
  s.homepage = 'https://github.com/hoedown/hoedown'
  s.license = 'MIT'
  s.author = { 'Natacha Porte' => '',
               'Vincent Marti' => '',
               'Devin Torres and the hoedown authors' => 'devin@devintorr.es',
               'Clemens Gruber (podspec)' => 'clemensgru@gmail.com' }
  s.source = { :git => 'https://github.com/hoedown/hoedown.git', :tag => '3.0.1' }
  s.requires_arc = false

  s.default_subspec = 'standard'

  s.subspec 'standard' do |ss|
    ss.source_files = 'src/*.{c,h}'
  end
end
