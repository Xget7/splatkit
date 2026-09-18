Pod::Spec.new do |s|
  s.name = 'SplatKitReactNative'
  s.version = '0.1.0-alpha.2'
  s.summary = 'Fabric view adapter for SplatKit native renderers'
  s.homepage = 'https://github.com/Xget7/react-native-splatkit'
  s.license = { :type => 'MIT' }
  s.author = { 'SplatKit' => 'opensource@splatkit.dev' }
  s.platforms = { :ios => '17.0' }
  s.source = { :git => 'https://github.com/Xget7/react-native-splatkit.git', :tag => "v#{s.version}" }
  s.source_files = 'ios/**/*.{h,mm}'
  s.public_header_files = 'ios/*.h'
  # The engine and SplatKit/SKSplatEngine.h; scripts/fetch-ios-xcframework.cjs installs it at npm prepack.
  s.vendored_frameworks = 'ios/SplatKitCore.xcframework'
  # The spz loader calls the system zlib.
  s.libraries = 'c++', 'z'
  s.frameworks = 'UIKit', 'QuartzCore', 'Metal'
  s.requires_arc = true

  # Defined by the app's Podfile (react_native_pods); adds the Fabric dependencies and flags, so it runs last.
  install_modules_dependencies(s)
end
